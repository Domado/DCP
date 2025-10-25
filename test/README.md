# 编译
g++ -o dcp_test test.cpp \
    -x c dcp.c \
    -x c dcp_scheduler.c \
    -x c dcp_allocator.c \
    -I. -std=c++11 -lpthread

# 运行测试
./dcp_test
