#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
using byte = uint8_t;
using String = std::string;
#define LOG_DEBUG(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define FILE_O_READ 0
#define FILE_O_WRITE 1
struct Lock { void lock() {} void unlock() {} };
inline Lock lock; inline Lock* spiLock = &lock;
namespace concurrency { struct LockGuard { LockGuard(Lock*) {} }; }
struct Print { virtual size_t write(uint8_t) = 0; virtual size_t write(const uint8_t*,size_t) = 0; virtual ~Print() = default; };
struct FS;
struct File {
    FS* fs; std::string path; size_t position = 0; bool opened = false;
    explicit File(FS& f):fs(&f) {}
    explicit operator bool() const { return opened; }
    size_t write(uint8_t b) { return write(&b,1); }
    size_t write(const uint8_t*,size_t);
    int read(); int read(uint8_t*,size_t); int available();
    void flush() {} void close() { opened = false; }
};
struct FS {
    std::map<std::string,std::vector<uint8_t>> files;
    bool failRename = false, failRemove = false;
    bool exists(const char* name) { return files.count(name); }
    bool remove(const char* name) { if(failRemove)return false; return files.erase(name); }
    File open(const char* name, int mode) {
        File f(*this); f.path=name;
        if(mode == FILE_O_READ && !exists(name)) return f;
        // Match STM32_LittleFS_File::_open_file: create/read-write, seek to end.
        f.position = mode == FILE_O_WRITE ? files[name].size() : 0; f.opened=true; return f;
    }
    bool rename(const char* from,const char* to) {
        if(failRename || !exists(from))return false;
        files[to]=files[from]; files.erase(from); return true;
    }
};
inline FS fs;
#define FSCom fs
inline size_t File::write(const uint8_t* p,size_t n) {
    auto& data=fs->files[path]; data.resize(position+n); memcpy(data.data()+position,p,n); position+=n; return n;
}
inline int File::read() { auto& data=fs->files[path];return position<data.size()?data[position++]:-1; }
inline int File::read(uint8_t* out,size_t count) { size_t n=0; for(;n<count && available();++n)out[n]=read();return n; }
inline int File::available() { return fs->files[path].size()-position; }
bool renameFile(const char*,const char*);
