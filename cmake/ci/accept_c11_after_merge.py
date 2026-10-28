#!/usr/bin/env python3
"""Post-merge C11 acceptance only. No old SDK/CPP workflow fallback or Mono."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build" / "post-merge-c11"
STAGE = BUILD / "stage"


def run(*argv, env=None, cwd=ROOT):
    cmd = [str(x) for x in argv]
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, cwd=cwd, env=env)


def source_tree(label):
    directory = ROOT / "build" / "upstream" / label
    if not (directory / "CMakeLists.txt").is_file():
        raise RuntimeError("Required immutable upstream source checkout missing: " + str(directory))
    head = subprocess.check_output(["git", "-C", str(directory), "rev-parse", "HEAD"],
                                   text=True).strip()
    print(f"source owner: {label} HEAD={head}", flush=True)
    return directory


def installed(name):
    return STAGE / name


def install_project(label, source, env, options=()):
    build = BUILD / ("build-" + label)
    prefix = installed(label)
    cmake = [
        "cmake", "-S", source, "-B", build, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        "-DBUILD_TESTING=OFF", "-DBUILD_TESTS=OFF",
        "-DBUILD_EXAMPLES=OFF", "-DBUILD_BENCHMARKS=OFF",
        "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
        "-DCMAKE_PREFIX_PATH=" + env["CMAKE_PREFIX_PATH"],
    ]
    toolchain = Path(env["VCPKG_CACHE_REPOSITORY_ROOT"]) / "cmake" / "QigaoVcpkgToolchain.cmake"
    if not toolchain.is_file():
        raise RuntimeError("Missing canonical vcpkg-cache toolchain: " + str(toolchain))
    cmake.append("-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain))
    # This is full-source, not consumer-only TSan: upstream Core, Salts3,
    # Crypto, Utils and Praktor are ALL instrumented in this workflow.
    sanitizer = env.get("PRAKTOR_UPSTREAM_SANITIZER", "")
    if sanitizer:
        options_flag = ("-fsanitize=thread" if sanitizer == "thread"
                        else "-fsanitize=address,undefined")
        compile_flags = options_flag + " -fno-omit-frame-pointer -O1 -g"
        cmake.extend(("-DCMAKE_C_FLAGS=" + compile_flags,
                      "-DCMAKE_CXX_FLAGS=" + compile_flags,
                      "-DCMAKE_EXE_LINKER_FLAGS=" + options_flag,
                      "-DCMAKE_SHARED_LINKER_FLAGS=" + options_flag))
    cmake.extend(options)
    run(*cmake, env=env)
    run("cmake", "--build", build, "--parallel", "2", "--config", "RelWithDebInfo", env=env)
    run("cmake", "--install", build, "--config", "RelWithDebInfo", env=env)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sanitizer", choices=["none", "address", "thread"], default="none")
    args = parser.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.setdefault("BUILD_TESTING", "OFF")
    if args.sanitizer != "none":
        env["PRAKTOR_UPSTREAM_SANITIZER"] = args.sanitizer
    if not env.get("VCPKG_CACHE_REPOSITORY_ROOT"):
        raise RuntimeError("Missing vcpkg-cache setup; no alternate provider is allowed")
    triplet = env.get("TRIPLET")
    if not triplet:
        raise RuntimeError("TRIPLET must be explicitly selected")

    sources = {name: source_tree(name) for name in ("salts-core", "salts", "crypto", "salts-utils")}
    env["CMAKE_PREFIX_PATH"] = ";".join(str(installed(name)) for name in
        ("salts-core", "crypto", "salts", "salts-utils"))

    install_project("salts-core", sources["salts-core"], env, [
        "-DSALTSCORE_BUILD_CSERDE=ON",
        "-DSALTSCORE_BUILD_UNICODE=ON",
        "-DSALTSCORE_BUILD_TINYTEST=ON",
        "-DSALTSCORE_BUILD_PROCESS=OFF",
    ])
    env["SALTSCORE_ROOT"] = str(installed("salts-core"))

    install_project("salts", sources["salts"], env)
    env["SALTS_ROOT"] = str(installed("salts"))

    # Crypto is the ONLY owner of static TLS/ASN1. GmSSL must come from its
    # pinned vcpkg-cache port; never reintroduce an OpenSSL/Legacy fallback.
    vcpkg_root = Path(env.get("VCPKG_ROOT", ""))
    vcpkg = vcpkg_root / ("vcpkg.exe" if sys.platform == "win32" else "vcpkg")
    installed_root = ROOT / "vcpkg_installed" / "ci"
    gmssl = installed_root / triplet
    if not (gmssl / "share" / "gmssl" / "GmSSLConfig.cmake").is_file():
        if not vcpkg.is_file():
            raise RuntimeError("Cannot provision canonical GmSSL: vcpkg executable absent")
        # The Crypto-only dependency must have its OWN manifest. Invoking
        # 'vcpkg install gmssl:<triplet>' from the Praktor manifest is illegal
        # and must not add a production GmSSL dependency to this repository.
        import json
        manifest_dir = BUILD / "crypto-gmssl-manifest"
        manifest_dir.mkdir(parents=True, exist_ok=True)
        manifest = {
            "name": "praktor-crypto-gmssl-ci",
            "version-string": "0.0.0",
            "dependencies": ["gmssl"],
            "builtin-baseline": "b1b19307e2d2ec1eefbdb7ea069de7d4bcd31f01",
        }
        (manifest_dir / "vcpkg.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        run(vcpkg, "install",
            "--x-manifest-root=" + str(manifest_dir),
            "--triplet=" + triplet,
            "--overlay-ports=" + str(Path(env["VCPKG_CACHE_REPOSITORY_ROOT"]) / "ports"),
            "--x-install-root=" + str(installed_root), env=env)
        if not (gmssl / "share" / "gmssl" / "GmSSLConfig.cmake").is_file():
            raise RuntimeError("Canonical GmSSL port is absent after manifest installation")
    install_project("crypto", sources["crypto"], env,
                    ["-DCRYPTO_GMSSL_ROOT=" + str(gmssl)])
    env["CRYPTO_ROOT"] = str(installed("crypto"))

    env["SALTS_UTILS_CI_BUILD_ROOT"] = str(BUILD / "build-salts-utils")
    install_project("salts-utils", sources["salts-utils"], env)
    env["SALTS_UTILS_ROOT"] = str(installed("salts-utils"))

    # All package identities are explicit. No CHttp, C++ compiler,
    # native-in-runtime copy or multi-provider fallback may be selected.
    runtime_build = BUILD / ("praktor-" + args.sanitizer)
    flags = []
    if args.sanitizer == "address":
        flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-O1", "-g"]
    elif args.sanitizer == "thread":
        flags = ["-fsanitize=thread", "-fno-omit-frame-pointer", "-O1", "-g"]
    cmake = [
        "cmake", "-S", ROOT, "-B", runtime_build, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        "-DENABLE_SCRIPT_ENGINE=OFF",
        "-DPRAKTOR_ENABLE_C11_ACTIONS=ON",
        "-DENABLE_TESTS=ON",
        "-DCMAKE_INSTALL_PREFIX=" + str(installed("praktor")),
        "-DCMAKE_PREFIX_PATH=" + env["CMAKE_PREFIX_PATH"],
        "-DCMAKE_TOOLCHAIN_FILE=" + str(
            Path(env["VCPKG_CACHE_REPOSITORY_ROOT"]) / "cmake" / "QigaoVcpkgToolchain.cmake"),
    ]
    if flags:
        cmake.extend(("-DCMAKE_C_FLAGS=" + " ".join(flags),
                      "-DCMAKE_EXE_LINKER_FLAGS=" + flags[0],
                      "-DCMAKE_SHARED_LINKER_FLAGS=" + flags[0]))
        if args.sanitizer == "thread":
            env["TSAN_OPTIONS"] = "halt_on_error=1:exitcode=66"
    run(*cmake, env=env)
    run("cmake", "--build", runtime_build, "--config", "RelWithDebInfo",
        "--parallel", "2", env=env)
    run("ctest", "--test-dir", runtime_build, "--config", "RelWithDebInfo",
        "--no-tests=error", "--output-on-failure", "--parallel", "1",
        "--timeout", "75", env=env)
    run("cmake", "--install", runtime_build, "--config", "RelWithDebInfo", env=env)

    if args.sanitizer == "none":
        consumer_build = BUILD / "installed-consumer"
        run("cmake", "-S", ROOT / "sdk" / "test" / "c-only-consumer",
            "-B", consumer_build, "-G", "Ninja",
            "-DCMAKE_PREFIX_PATH=" + ";".join((str(installed("praktor")),
                env["CMAKE_PREFIX_PATH"])), env=env)
        run("cmake", "--build", consumer_build, "--parallel", "2", env=env)
        run("ctest", "--test-dir", consumer_build, "--no-tests=error",
            "--output-on-failure", env=env)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit("C11 POST-MERGE RED: " + str(error))
