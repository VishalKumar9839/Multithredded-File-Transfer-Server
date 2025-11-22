#pragma once
#include <string>
#include <algorithm>
#include <sys/stat.h>


inline std::string sanitize_path(const std::string &root, const std::string &path) {
// remove query part
size_t q = path.find('?');
std::string p = (q==std::string::npos) ? path : path.substr(0,q);
// simple normalization: remove ".." and leading '/'
std::string out;
size_t i=0;
while(i < p.size() && p[i]=='/') ++i;
while(i < p.size()) {
size_t j = p.find('/', i);
std::string token = (j==std::string::npos) ? p.substr(i) : p.substr(i, j-i);
if(token == "..") {
// remove last segment
size_t pos = out.find_last_of('/');
if(pos==std::string::npos) out.clear(); else out.erase(pos);
} else if(token.size() && token != ".") {
if(!out.empty()) out.push_back('/');
out += token;
}
if(j==std::string::npos) break; else i = j+1;
}
if(out.empty()) out = "index.html";
std::string full = root + "/" + out;
return full;
}


inline bool file_exists(const std::string &path) {
struct stat st;
return stat(path.c_str(), &st)==0 && S_ISREG(st.st_mode);
}


inline off_t file_size(const std::string &path) {
struct stat st;
if(stat(path.c_str(), &st)==0) return st.st_size;
return -1;
}