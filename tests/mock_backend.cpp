#include <atomic>
static std::atomic<int> enabled{0};
extern "C" __declspec(dllexport) void RexSmaaEnable(int value){enabled=value?1:0;}
extern "C" __declspec(dllexport) int RexSmaaStatus(){return enabled?2:0;}
extern "C" __declspec(dllexport) int TestSmaaEnabled(){return enabled;}
