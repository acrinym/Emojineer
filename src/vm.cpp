#include "emojineer/vm.hpp"
#include "emojineer/unicode.hpp"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <istream>
#include <iterator>
#include <limits>
#include <memory>
#include <ostream>
#include <random>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef EMOJINEER_HAVE_CURL
#include <curl/curl.h>
#endif

namespace emojineer { namespace {
constexpr std::size_t MaxCallDepth = 4096;

template<typename Fail>
std::int64_t checked_add(std::int64_t a, std::int64_t b, std::uint32_t line, Fail fail) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        fail(line, "integer overflow: ➕ overflow");
    return a + b;
}

template<typename Fail>
std::int64_t checked_sub(std::int64_t a, std::int64_t b, std::uint32_t line, Fail fail) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
        fail(line, "integer overflow: ➖ overflow");
    return a - b;
}

template<typename Fail>
std::int64_t checked_mul(std::int64_t a, std::int64_t b, std::uint32_t line, Fail fail) {
    if (a == 0 || b == 0) return 0;
    if ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN))
        fail(line, "integer overflow: ✖️ overflow");
    if (a > 0) {
        if (b > 0) {
            if (a > INT64_MAX / b) fail(line, "integer overflow: ✖️ overflow");
        } else if (b < INT64_MIN / a) {
            fail(line, "integer overflow: ✖️ overflow");
        }
    } else {
        if (b > 0) {
            if (a < INT64_MIN / b) fail(line, "integer overflow: ✖️ overflow");
        } else if (a > INT64_MAX / b) {
            fail(line, "integer overflow: ✖️ overflow");
        }
    }
    return a * b;
}

constexpr std::size_t MaxFilesystemReadBytes = 16 * 1024 * 1024;
constexpr std::size_t MaxNetworkReadBytes = 1024 * 1024;

std::string require_native_string(const Value& value, const std::string& facility) {
    if (const auto* text = std::get_if<std::string>(&value)) return *text;
    throw std::runtime_error(facility + " requires a text argument");
}

std::uint64_t require_positive_bound(const Value& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        if (*integer > 0) return static_cast<std::uint64_t>(*integer);
    } else if (const auto* number = std::get_if<double>(&value)) {
        if (std::isfinite(*number) && std::floor(*number) == *number && *number > 0 &&
            *number <= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
            return static_cast<std::uint64_t>(*number);
    }
    throw std::runtime_error("random.int requires a positive whole-number bound");
}

std::string read_host_text(const std::string& raw_path) {
    if (raw_path.empty()) throw std::runtime_error("filesystem.read-text path cannot be empty");
    const std::filesystem::path path(raw_path);
#ifdef _WIN32
    struct HandleGuard {
        HANDLE handle{INVALID_HANDLE_VALUE};
        ~HandleGuard() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    };
    HandleGuard file{CreateFileW(path.c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (file.handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("filesystem.read-text cannot open requested path");
    if (GetFileType(file.handle) != FILE_TYPE_DISK)
        throw std::runtime_error("filesystem.read-text requires a regular file");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.handle, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        throw std::runtime_error("filesystem.read-text requires a regular file");
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.handle, &size) || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > MaxFilesystemReadBytes)
        throw std::runtime_error("filesystem.read-text exceeds 16 MiB limit");
    std::string result(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < result.size()) {
        const auto remaining = result.size() - offset;
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(remaining, 1u << 20));
        DWORD read = 0;
        if (!ReadFile(file.handle, result.data() + offset, request, &read, nullptr))
            throw std::runtime_error("filesystem.read-text failed while reading path");
        if (read == 0) break;
        offset += read;
    }
    if (offset != result.size())
        throw std::runtime_error("filesystem.read-text file changed while reading");
    return result;
#else
    struct FdGuard {
        int fd{-1};
        ~FdGuard() { if (fd >= 0) ::close(fd); }
    };
    int flags = O_RDONLY | O_NONBLOCK;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    FdGuard file{::open(path.c_str(), flags)};
    if (file.fd < 0)
        throw std::runtime_error("filesystem.read-text cannot open requested path");
    struct stat status{};
    if (::fstat(file.fd, &status) != 0)
        throw std::runtime_error("filesystem.read-text cannot inspect requested path");
    if (!S_ISREG(status.st_mode))
        throw std::runtime_error("filesystem.read-text requires a regular file");
    if (status.st_size < 0 || static_cast<std::uint64_t>(status.st_size) > MaxFilesystemReadBytes)
        throw std::runtime_error("filesystem.read-text exceeds 16 MiB limit");
    std::string result(static_cast<std::size_t>(status.st_size), '\0');
    std::size_t offset = 0;
    while (offset < result.size()) {
        const auto count = ::read(file.fd, result.data() + offset, result.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) throw std::runtime_error("filesystem.read-text failed while reading path");
        if (count == 0) break;
        offset += static_cast<std::size_t>(count);
    }
    if (offset != result.size())
        throw std::runtime_error("filesystem.read-text file changed while reading");
    return result;
#endif
}

bool valid_environment_name(const std::string& name) {
    if (name.empty() || name.size() > 256) return false;
    const auto first = static_cast<unsigned char>(name.front());
    if (!(std::isalpha(first) || name.front() == '_')) return false;
    return std::all_of(name.begin() + 1, name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_';
    });
}

#ifdef EMOJINEER_HAVE_CURL
struct CurlBuffer {
    std::string data;
    bool overflow{false};
};

std::size_t curl_write(void* contents, std::size_t size, std::size_t count, void* user) {
    auto* buffer = static_cast<CurlBuffer*>(user);
    if (count != 0 && size > std::numeric_limits<std::size_t>::max() / count) {
        buffer->overflow = true;
        return 0;
    }
    const auto bytes = size * count;
    if (buffer->data.size() > MaxNetworkReadBytes || bytes > MaxNetworkReadBytes - buffer->data.size()) {
        buffer->overflow = true;
        return 0;
    }
    buffer->data.append(static_cast<const char*>(contents), bytes);
    return bytes;
}
#endif

std::string https_get(const std::string& url) {
    if (url.size() > 8192 || url.rfind("https://", 0) != 0)
        throw std::runtime_error("network.get requires a bounded https:// URL");
    const auto authority_end = url.find('/', 8);
    const auto authority = url.substr(8, authority_end == std::string::npos ? std::string::npos : authority_end - 8);
    if (authority.empty() || authority.find('@') != std::string::npos ||
        url.find('\r') != std::string::npos || url.find('\n') != std::string::npos)
        throw std::runtime_error("network.get rejected URL authority");
#ifndef EMOJINEER_HAVE_CURL
    throw std::runtime_error("network.get is unavailable because this build has no libcurl support");
#else
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), &curl_easy_cleanup);
    if (!handle) throw std::runtime_error("network.get could not initialize libcurl");
    CurlBuffer buffer;
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 0L);
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "Emojineer/0.20");
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &buffer);
    const auto result = curl_easy_perform(handle.get());
    if (buffer.overflow) throw std::runtime_error("network.get response exceeds 1 MiB limit");
    if (result != CURLE_OK) throw std::runtime_error(std::string("network.get failed: ") + curl_easy_strerror(result));
    long status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status >= 300)
        throw std::runtime_error("network.get received HTTP status " + std::to_string(status));
    return buffer.data;
#endif
}
} // anonymous namespace

VM::VM(std::istream& i, std::ostream& o, std::uint64_t fuel, ExecutionPolicy policy)
    : input_(i), output_(o), fuel_(fuel), remaining_fuel_(fuel), policy_(std::move(policy)) {
    validate_execution_policy(policy_);
    deterministic_random_state_ = policy_.deterministic_seed ? policy_.deterministic_seed : 0x9E3779B97F4A7C15ULL;
    deterministic_clock_ms_ = policy_.deterministic_clock_ms;
}

std::optional<SourcePosition> VM::get_current_source_position() const {
    if (!current_chunk_ || ip_ >= current_chunk_->source_map.size()) {
        return std::nullopt;
    }
    const auto& src = current_chunk_->source_map[ip_];
    return SourcePosition(src.source_path, src.line, src.column);
}

std::vector<DebugFrame> VM::get_call_stack() const {
    // Build frames in reverse order so frame 0 is innermost (currently executing)
    // This matches debugger conventions where frame 0 is the current function
    std::vector<DebugFrame> stack;
    
    // Determine which frame is currently executing (innermost)
    // The current instruction pointer tells us which frame we're in
    std::size_t innermost_idx = frames_.empty() ? 0 : frames_.size() - 1;
    
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        const auto& f = frames_[i];
        DebugFrame frame;
        // Frame 0 is innermost (most recent call), frames increase outward
        frame.frame_index = frames_.size() - 1 - i;
        
        if (f.function_index < current_chunk_->functions.size()) {
            frame.function_name = current_chunk_->functions[f.function_index].name;
        }
        
        // For the innermost (currently executing) frame, use current IP
        // For suspended callers, use return IP - 1 (where they will resume)
        if (i == innermost_idx) {
            // Current executing frame - use current IP
            if (ip_ > 0 && ip_ < current_chunk_->source_map.size()) {
                const auto& src = current_chunk_->source_map[ip_];
                frame.source_position = SourcePosition(src.source_path, src.line, src.column);
            }
        } else {
            // Suspended caller - use return IP - 1 (where it will resume)
            if (f.return_ip > 0 && f.return_ip < current_chunk_->source_map.size()) {
                const auto& src = current_chunk_->source_map[f.return_ip - 1];
                frame.source_position = SourcePosition(src.source_path, src.line, src.column);
            }
        }
        
        if (f.function_index < current_chunk_->functions.size()) {
            const auto& fn = current_chunk_->functions[f.function_index];
            frame.parameters.resize(std::min<std::size_t>(fn.arity, f.locals.size()));
            for (std::size_t p = 0; p < frame.parameters.size(); ++p) {
                frame.parameters[p] = f.locals[p];
            }
            // Copy parameter names for debugging
            frame.parameter_names = fn.parameter_names;
            // Trim to actual parameter count
            if (frame.parameter_names.size() > frame.parameters.size()) {
                frame.parameter_names.resize(frame.parameters.size());
            }
            
            // Trim locals to exclude parameters (frame.locals contains only true locals)
            frame.locals.resize(f.locals.size() > fn.arity ? f.locals.size() - fn.arity : 0);
            for (std::size_t l = fn.arity; l < f.locals.size(); ++l) {
                frame.locals[l - fn.arity] = f.locals[l];
            }
            
            // Copy local names for debugging, offset by arity to match trimmed locals
            // fn.local_names includes parameters + locals, but frame.locals only has locals
            for (std::size_t l = fn.arity; l < fn.local_names.size(); ++l) {
                frame.local_names.push_back(fn.local_names[l]);
            }
            
            // Copy parameter names
            frame.parameter_names = fn.parameter_names;
            if (frame.parameter_names.size() > frame.parameters.size()) {
                frame.parameter_names.resize(frame.parameters.size());
            }
            
            // Populate named parameters from FunctionInfo metadata
            for (std::size_t p = 0; p < fn.parameter_names.size() && p < f.locals.size(); ++p) {
                frame.named_parameters[fn.parameter_names[p]] = f.locals[p];
            }
            
            // Populate named locals from FunctionInfo::local_names (includes parameters + local vars)
            for (std::size_t slot = 0; slot < fn.local_names.size() && slot < f.locals.size(); ++slot) {
                frame.named_locals[fn.local_names[slot]] = f.locals[slot];
            }
        }
        
        // Add globals for each frame (the VM's global state)
        frame.globals = globals_;
        
        stack.push_back(std::move(frame));
    }
    
    // Reverse so frame 0 is innermost
    std::reverse(stack.begin(), stack.end());
    // Re-index to ensure 0 = innermost
    for (std::size_t i = 0; i < stack.size(); ++i) {
        stack[i].frame_index = i;
    }
    
    return stack;
}

std::unordered_map<std::string, Value> VM::get_globals() const {
    return globals_;
}

void VM::initialize_execution(const Chunk& c) {
    verify_bytecode(c);
    require_execution_capabilities(c.required_capabilities, policy_);
    deterministic_random_state_ = policy_.deterministic_seed ? policy_.deterministic_seed : 0x9E3779B97F4A7C15ULL;
    deterministic_clock_ms_ = policy_.deterministic_clock_ms;
    stack_.clear();
    frames_.clear();
    globals_.clear();
    ip_ = 0;
    current_chunk_ = &c;
    remaining_fuel_ = fuel_;
    execution_finished_ = false;
    initial_execution_ = false;
}

void VM::execute(const Chunk& c) {
    if (initial_execution_ || current_chunk_ != &c) {
        initialize_execution(c);
    }
    run_execution_loop();
}

void VM::run_execution_loop() {
    if (!current_chunk_ || execution_finished_) return;
    const Chunk& c = *current_chunk_;
    
    while (ip_ < c.code.size()) {
        // Check if debugger wants to pause BEFORE executing this instruction
        // This ensures we don't execute any instruction when paused, allowing
        // zero-effect debugger preparation and breakpoint setting before execution
        if (debug_control_) {
            if (debug_control_->should_pause_before_execution()) {
                debug_paused_ = true;
                return;
            }
        }
        
        // Check fuel BEFORE executing instruction
        if (remaining_fuel_ == 0)
            runtime_error(c.code[ip_].line, "execution fuel exhausted (possible infinite loop)");
        
        // Execute the instruction first, then consume fuel - this ensures paused
        // programs don't lose fuel for instructions they haven't actually executed
        const Instruction ins = c.code[ip_++];
        const auto line = ins.line;
        
        switch (ins.op) {
            case OpCode::Constant:
                stack_.push_back(c.constants.at(static_cast<std::size_t>(ins.operand)));
                break;
                
            case OpCode::LoadGlobal: {
                auto n = constant_string(c, ins.operand, line);
                auto it = globals_.find(n);
                if (it == globals_.end())
                    runtime_error(line, "undefined emoji variable '" + n + "'");
                stack_.push_back(it->second);
                break;
            }
            
            case OpCode::StoreGlobal: {
                auto n = constant_string(c, ins.operand, line);
                globals_[n] = pop(line);
                break;
            }
            
            case OpCode::LoadLocal: {
                auto& f = frame(line);
                auto s = static_cast<std::size_t>(ins.operand);
                if (s >= f.locals.size()) runtime_error(line, "invalid local slot");
                stack_.push_back(f.locals[s]);
                break;
            }
            
            case OpCode::StoreLocal: {
                auto& f = frame(line);
                auto s = static_cast<std::size_t>(ins.operand);
                if (s >= f.locals.size()) runtime_error(line, "invalid local slot");
                f.locals[s] = pop(line);
                break;
            }
            
            case OpCode::AssertNumber:
                if (!std::holds_alternative<double>(peek(line)) && !std::holds_alternative<std::int64_t>(peek(line)))
                    runtime_error(line, "🔢 variable requires a number");
                break;
                
            case OpCode::AssertString:
                if (!std::holds_alternative<std::string>(peek(line)))
                    runtime_error(line, "🔤 variable requires text");
                break;
                
            case OpCode::AssertBool:
                if (!std::holds_alternative<bool>(peek(line)))
                    runtime_error(line, "🎯 variable requires ✅ or ❌");
                break;
                
            case OpCode::AssertArray:
                if (!std::holds_alternative<ArrayPtr>(peek(line)))
                    runtime_error(line, "📚 variable requires an array");
                break;
                
            case OpCode::Add: {
                Value r = pop(line), l = pop(line);
                if (auto* ln = std::get_if<double>(&l)) {
                    if (auto* rn = std::get_if<double>(&r))
                        stack_.emplace_back(*ln + *rn);
                    else
                        runtime_error(line, "➕ requires two numbers or two text values");
                } else if (auto* li = std::get_if<std::int64_t>(&l)) {
                    if (auto* ri = std::get_if<std::int64_t>(&r))
                        stack_.emplace_back(checked_add(*li, *ri, line, [this](auto q, const auto& m) { runtime_error(q, m); }));
                    else
                        runtime_error(line, "➕ requires two numbers or two text values");
                } else if (auto* ls = std::get_if<std::string>(&l)) {
                    if (auto* rs = std::get_if<std::string>(&r))
                        stack_.emplace_back(*ls + *rs);
                    else
                        runtime_error(line, "➕ requires two numbers or two text values");
                } else {
                    runtime_error(line, "➕ requires two numbers or two text values");
                }
                break;
            }
            
            case OpCode::Subtract: {
                double r = pop_number(line), l = pop_number(line);
                stack_.emplace_back(l - r);
                break;
            }
            
            case OpCode::Multiply: {
                double r = pop_number(line), l = pop_number(line);
                stack_.emplace_back(l * r);
                break;
            }
            
            case OpCode::Divide: {
                double r = pop_number(line), l = pop_number(line);
                if (r == 0) runtime_error(line, "division by zero");
                stack_.emplace_back(l / r);
                break;
            }
            
            case OpCode::Modulo: {
                double r = pop_number(line), l = pop_number(line);
                if (r == 0) runtime_error(line, "modulo by zero");
                stack_.emplace_back(std::fmod(l, r));
                break;
            }
            
            case OpCode::AddInt: {
                auto r = pop_int64(line), l = pop_int64(line);
                stack_.emplace_back(checked_add(l, r, line, [this](auto q, const auto& m) { runtime_error(q, m); }));
                break;
            }
            
            case OpCode::SubtractInt: {
                auto r = pop_int64(line), l = pop_int64(line);
                stack_.emplace_back(checked_sub(l, r, line, [this](auto q, const auto& m) { runtime_error(q, m); }));
                break;
            }
            
            case OpCode::MultiplyInt: {
                auto r = pop_int64(line), l = pop_int64(line);
                stack_.emplace_back(checked_mul(l, r, line, [this](auto q, const auto& m) { runtime_error(q, m); }));
                break;
            }
            
            case OpCode::Equal: {
                Value r = pop(line), l = pop(line);
                stack_.emplace_back(values_equal(l, r));
                break;
            }
            
            case OpCode::Less: {
                double r = pop_number(line), l = pop_number(line);
                stack_.emplace_back(l < r);
                break;
            }
            
            case OpCode::Greater: {
                double r = pop_number(line), l = pop_number(line);
                stack_.emplace_back(l > r);
                break;
            }
            
            case OpCode::Negate:
                stack_.emplace_back(-pop_number(line));
                break;
                
            case OpCode::Not:
                stack_.emplace_back(!pop_bool(line));
                break;
                
            case OpCode::ReadLine: {
                std::string v;
                if (!std::getline(input_, v))
                    runtime_error(line, "📥 could not read input");
                stack_.emplace_back(std::move(v));
                break;
            }
            
            case OpCode::Print:
                output_ << value_to_string(pop(line)) << '\n';
                break;
                
            case OpCode::JumpIfFalse: {
                bool cond = pop_bool(line);
                if (!cond) ip_ = static_cast<std::size_t>(ins.operand);
                break;
            }
            
            case OpCode::Jump:
                ip_ = static_cast<std::size_t>(ins.operand);
                break;
                
            case OpCode::Call: {
                auto fi = static_cast<std::size_t>(ins.operand);
                const auto& fn = c.functions[fi];
                if (stack_.size() < fn.arity)
                    runtime_error(line, "not enough call arguments on VM stack");
                if (frames_.size() >= MaxCallDepth)
                    runtime_error(line, "maximum function call depth exceeded");
                CallFrame f;
                f.return_ip = ip_;
                f.stack_base = stack_.size() - fn.arity;
                f.function_index = fi;
                f.locals.resize(fn.local_count, false);
                for (std::size_t n = fn.arity; n > 0; --n)
                    f.locals[n - 1] = pop(line);
                frames_.push_back(std::move(f));
                ip_ = fn.entry;
                break;
            }
            
            case OpCode::HostCall:
                execute_host_call(ins.operand, line);
                break;

            case OpCode::Return: {
                if (frames_.empty())
                    runtime_error(line, "Return executed outside a function");
                Value result = pop(line);
                CallFrame f = std::move(frames_.back());
                frames_.pop_back();
                if (stack_.size() != f.stack_base)
                    runtime_error(line, "function returned with leaked operand stack values");
                ip_ = f.return_ip;
                stack_.push_back(std::move(result));
                break;
            }
            
            case OpCode::MakeArray: {
                auto count = static_cast<std::size_t>(ins.operand);
                if (stack_.size() < count)
                    runtime_error(line, "not enough values for array literal");
                auto a = std::make_shared<ArrayValue>();
                a->elements.resize(count);
                for (std::size_t n = count; n > 0; --n)
                    a->elements[n - 1] = pop(line);
                stack_.emplace_back(std::move(a));
                break;
            }
            
            case OpCode::Index: {
                Value iv = pop(line), cv = pop(line);
                auto* ap = std::get_if<ArrayPtr>(&cv);
                if (!ap || !*ap) runtime_error(line, "🔎 requires an array");
                double raw;
                if (auto* d = std::get_if<double>(&iv)) raw = *d;
                else if (auto* i = std::get_if<std::int64_t>(&iv)) raw = static_cast<double>(*i);
                else runtime_error(line, "🔎 index must be a whole number");
                if (!std::isfinite(raw) || std::floor(raw) != raw || raw < 0 || raw >= static_cast<double>((*ap)->elements.size()))
                    runtime_error(line, "🔎 array index out of range");
                stack_.push_back((*ap)->elements[static_cast<std::size_t>(raw)]);
                break;
            }
            
            case OpCode::Length: {
                Value v = pop(line);
                if (auto* ap = std::get_if<ArrayPtr>(&v)) {
                    if (!*ap) runtime_error(line, "📏 received a null array");
                    stack_.emplace_back(static_cast<double>((*ap)->elements.size()));
                } else if (auto* text = std::get_if<std::string>(&v)) {
                    stack_.emplace_back(static_cast<double>(segment_graphemes(*text).size()));
                } else {
                    runtime_error(line, "📏 requires an array or text value");
                }
                break;
            }
            
            case OpCode::Append: {
                Value value = pop(line), cv = pop(line);
                auto* ap = std::get_if<ArrayPtr>(&cv);
                if (!ap || !*ap) runtime_error(line, "📎 requires an array");
                auto next = std::make_shared<ArrayValue>(**ap);
                next->elements.push_back(std::move(value));
                stack_.emplace_back(std::move(next));
                break;
            }
            
            case OpCode::SetIndex: {
                Value value = pop(line), iv = pop(line), cv = pop(line);
                auto* ap = std::get_if<ArrayPtr>(&cv);
                if (!ap || !*ap) runtime_error(line, "🧷 requires an array");
                double raw;
                if (auto* d = std::get_if<double>(&iv)) raw = *d;
                else if (auto* i = std::get_if<std::int64_t>(&iv)) raw = static_cast<double>(*i);
                else runtime_error(line, "🧷 index must be a whole number");
                if (!std::isfinite(raw) || std::floor(raw) != raw || raw < 0 || raw >= static_cast<double>((*ap)->elements.size()))
                    runtime_error(line, "🧷 array index out of range");
                auto next = std::make_shared<ArrayValue>(**ap);
                next->elements[static_cast<std::size_t>(raw)] = std::move(value);
                stack_.emplace_back(std::move(next));
                break;
            }
            
            case OpCode::Halt:
                if (!frames_.empty()) runtime_error(line, "VM halted inside a function");
                if (!stack_.empty()) runtime_error(line, "VM halted with a non-empty stack");
                execution_finished_ = true;
                return;
        }
        
        // Consume fuel AFTER successful instruction execution - this ensures
        // debug pauses don't consume extra fuel that normal execution wouldn't
        --remaining_fuel_;
        
        // Check for step mode pause AFTER executing instruction
        if (debug_control_) {
            if (!debug_control_->debug_hook()) {
                debug_paused_ = true;
                return;
            }
        }
    }
    
    runtime_error(0, "bytecode terminated without Halt");
}

std::uint64_t VM::next_deterministic_random() {
    std::uint64_t x = deterministic_random_state_;
    if (x == 0) x = 0x9E3779B97F4A7C15ULL;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    deterministic_random_state_ = x;
    return x * 2685821657736338717ULL;
}

void VM::execute_host_call(std::int32_t operand, std::uint32_t line) {
    const auto facility = native_facility_from_operand(operand);
    if (!facility) runtime_error(line, "invalid native facility operand");
    const auto capability = native_facility_capability(*facility);
    const auto bit = capability_mask(capability);
    if ((policy_.grants & bit) == 0)
        runtime_error(line, "native facility requires capability grant: " + capability_name(capability));
    if (policy_.mode == ExecutionMode::Sandbox)
        runtime_error(line, "native facilities are unavailable in sandbox mode");
    if (policy_.mode == ExecutionMode::Deterministic &&
        capability != Capability::Clock && capability != Capability::Random)
        runtime_error(line, "real host facilities are unavailable in deterministic mode");

    const auto arity = native_facility_arity(*facility);
    if (stack_.size() < arity) runtime_error(line, "not enough native call arguments on VM stack");
    std::vector<Value> args(arity);
    for (std::size_t n = arity; n > 0; --n) args[n - 1] = pop(line);

    try {
        switch (*facility) {
            case NativeFacility::FilesystemReadText:
                stack_.emplace_back(read_host_text(require_native_string(args[0], native_facility_name(*facility))));
                return;
            case NativeFacility::NetworkGet:
                stack_.emplace_back(https_get(require_native_string(args[0], native_facility_name(*facility))));
                return;
            case NativeFacility::ProcessRun: {
                const auto command = require_native_string(args[0], native_facility_name(*facility));
                if (command.empty() || command.size() > 32768 || command.find('\0') != std::string::npos)
                    throw std::runtime_error("process.run requires a non-empty command up to 32 KiB");
                const int status = std::system(command.c_str());
                if (status == -1) throw std::runtime_error("process.run could not invoke the host command processor");
                stack_.emplace_back(static_cast<std::int64_t>(status));
                return;
            }
            case NativeFacility::ClockMillis:
                if (policy_.mode == ExecutionMode::Deterministic) {
                    stack_.emplace_back(deterministic_clock_ms_++);
                } else {
                    const auto now = std::chrono::system_clock::now().time_since_epoch();
                    stack_.emplace_back(static_cast<std::int64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(now).count()));
                }
                return;
            case NativeFacility::RandomInt: {
                const auto bound = require_positive_bound(args[0]);
                std::uint64_t value = 0;
                if (policy_.mode == ExecutionMode::Deterministic) {
                    const auto limit = std::numeric_limits<std::uint64_t>::max() -
                                       (std::numeric_limits<std::uint64_t>::max() % bound);
                    do { value = next_deterministic_random(); } while (value >= limit);
                    value %= bound;
                } else {
                    std::random_device source;
                    const auto seed = (static_cast<std::uint64_t>(source()) << 32) ^
                                      static_cast<std::uint64_t>(source());
                    std::mt19937_64 engine(seed);
                    std::uniform_int_distribution<std::uint64_t> distribution(0, bound - 1);
                    value = distribution(engine);
                }
                stack_.emplace_back(static_cast<std::int64_t>(value));
                return;
            }
            case NativeFacility::HostEnvironment: {
                const auto name = require_native_string(args[0], native_facility_name(*facility));
                if (!valid_environment_name(name))
                    throw std::runtime_error("host.environment requires a portable environment variable name");
                const char* value = std::getenv(name.c_str());
                stack_.emplace_back(value ? std::string(value) : std::string{});
                return;
            }
        }
    } catch (const std::exception& error) {
        runtime_error(line, native_facility_name(*facility) + ": " + error.what());
    }
}

Value VM::pop(std::uint32_t line) {
    if (stack_.empty()) runtime_error(line, "VM stack underflow");
    Value v = std::move(stack_.back());
    stack_.pop_back();
    return v;
}

const Value& VM::peek(std::uint32_t line) const {
    if (stack_.empty()) runtime_error(line, "VM stack underflow");
    return stack_.back();
}

bool VM::pop_bool(std::uint32_t line) {
    Value v = pop(line);
    if (auto* b = std::get_if<bool>(&v)) return *b;
    runtime_error(line, "condition requires ✅ or ❌");
}

double VM::pop_number(std::uint32_t line) {
    Value v = pop(line);
    if (auto* n = std::get_if<double>(&v)) return *n;
    if (auto* i = std::get_if<std::int64_t>(&v)) return static_cast<double>(*i);
    runtime_error(line, "numeric operation requires numbers");
}

std::int64_t VM::pop_int64(std::uint32_t line) {
    Value v = pop(line);
    if (auto* i = std::get_if<std::int64_t>(&v)) return *i;
    runtime_error(line, "integer operation requires integers");
}

std::string VM::constant_string(const Chunk& c, std::int32_t idx, std::uint32_t line) const {
    if (idx < 0 || static_cast<std::size_t>(idx) >= c.constants.size())
        runtime_error(line, "invalid constant index");
    auto* s = std::get_if<std::string>(&c.constants[static_cast<std::size_t>(idx)]);
    if (!s) runtime_error(line, "bytecode expected string constant");
    return *s;
}

VM::CallFrame& VM::frame(std::uint32_t line) {
    if (frames_.empty()) runtime_error(line, "local variable access outside function");
    return frames_.back();
}

const VM::CallFrame& VM::frame(std::uint32_t line) const {
    if (frames_.empty()) runtime_error(line, "local variable access outside function");
    return frames_.back();
}

[[noreturn]] void VM::runtime_error(std::uint32_t line, const std::string& m) const {
    if (line) throw std::runtime_error("runtime line " + std::to_string(line) + ": " + m);
    throw std::runtime_error("runtime: " + m);
}

} // namespace emojineer
