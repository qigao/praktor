#ifndef __DOT_WRITER_H__
#define __DOT_WRITER_H__

#include <fmt/core.h>
#include <fstream>
#include <string>

class DotWriter {
public:
    virtual ~DotWriter() = default;
    virtual void write(std::string const& content) = 0;
    virtual void close() = 0;
};

class FileWriter : public DotWriter {
public:
    explicit FileWriter(std::string const& filename) : file(filename) {}

    void write(std::string const& content) override {
        if (file.is_open()) {
            file << content;
            file.close();
        }
    }

    void close() override {
        if (file.is_open()) { file.close(); }
    }

private:
    std::ofstream file;
};

#endif   // __DOT_WRITER_H__
