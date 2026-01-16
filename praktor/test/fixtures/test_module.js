// Test module for external file import
export function greet(name) {
    return `Hello, ${name}!`;
}

export function add(a, b) {
    return a + b;
}

export default {
    greet,
    add,
    version: "1.0.0"
};
