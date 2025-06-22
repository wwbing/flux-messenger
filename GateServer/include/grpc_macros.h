#pragma once

// 解决 INTERNAL 宏冲突问题
#ifdef INTERNAL
#undef INTERNAL
#endif

// 包含所有 gRPC 相关的头文件
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/status.h>
#include <grpcpp/support/status_code_enum.h> 