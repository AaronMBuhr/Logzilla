#include <iostream>

int main() {
    static constexpr const char* read_only = "test";
    
    // This will compile but crash at runtime with access violation
    char* ptr = const_cast<char*>(read_only);
    ptr[0] = 'X';  // Try to modify read-only memory
    
    return 0;
}
