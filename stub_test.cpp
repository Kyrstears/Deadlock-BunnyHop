#include <cstdio>
extern "C" int RunStubTest();
int main() {
    const int result=RunStubTest();
    std::puts(result ? "Command stub checks failed." :
        "Command stub checks passed.");
    return result;
}
