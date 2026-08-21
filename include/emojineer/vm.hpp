#pragma once

#include "emojineer/bytecode.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

namespace emojineer {

class VM {
public:
    VM(std::istream& input, std::ostream& output, std::uint64_t fuel = 1'000'000);
    void execute(const Chunk& chunk);

private:
    Value pop(std::uint32_t line);
    const Value& peek(std::uint32_t line) const;
    bool pop_bool(std::uint32_t line);
    double pop_number(std::uint32_t line);
    std::string constant_string(const Chunk& chunk, std::int32_t index, std::uint32_t line) const;
    [[noreturn]] void runtime_error(std::uint32_t line, const std::string& message) const;

    std::istream& input_;
    std::ostream& output_;
    std::uint64_t fuel_;
    std::vector<Value> stack_;
    std::unordered_map<std::string, Value> globals_;
};

} // namespace emojineer
