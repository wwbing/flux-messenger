import requests
import time
import concurrent.futures
import sys
import os

# --- 配置 ---
# GateServer 的地址
URL = "http://127.0.0.1:8080/get_test"
# 每一轮测试的请求数
REQUESTS_PER_STEP = 2000

# --- 梯度加压配置 ---
# 起始并发数
START_CONCURRENCY = 50
# 每次增加的并发数
STEP_INCREASE = 50
# 最大并发数
MAX_CONCURRENCY = 500


def send_request(n):
    """发送单个HTTP GET请求并记录结果和耗时"""
    start_time = time.time()
    try:
        # 定义一个空代理字典，以覆盖系统代理设置
        proxies = {
          "http": None,
          "https": None,
        }
        params = {'request_id': n, 'user': f'tester_{n}'}
        response = requests.get(URL, params=params, timeout=10, proxies=proxies)
        
        duration = time.time() - start_time
        
        # 1. 检查状态码
        if response.status_code != 200:
            return False, duration
            
        # 2. 检查响应体是否包含预期的关键字
        if "receive get_test req" not in response.text:
            return False, duration
            
        # 只有当状态码和响应体都正确时，才算成功
        return True, duration
            
    except requests.exceptions.RequestException as e:
        duration = time.time() - start_time
        return False, duration

def run_test_step(concurrency):
    """运行一轮指定并发数的测试"""
    print(f"\n--- 测试开始: 并发数 = {concurrency} ---")
    
    success_count = 0
    failure_count = 0
    total_times = []
    
    start_time = time.time()
    
    with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as executor:
        futures = [executor.submit(send_request, i) for i in range(REQUESTS_PER_STEP)]
        
        for future in concurrent.futures.as_completed(futures):
            is_success, duration = future.result()
            
            if is_success:
                success_count += 1
                total_times.append(duration)
            else:
                failure_count += 1

    end_time = time.time()
    total_duration = end_time - start_time
    
    rps = 0
    avg_time = 0
    
    if total_duration > 0 and success_count > 0:
        rps = success_count / total_duration
        avg_time = (sum(total_times) / success_count) * 1000

    print(f"测试耗时: {total_duration:.2f} s")
    print(f"成功/失败: {success_count}/{failure_count}")
    print(f"RPS: {rps:.2f}")
    print(f"平均响应时间: {avg_time:.2f} ms")
    
    return concurrency, rps, avg_time

def main():
    """主函数，运行梯度压力测试"""
    print("--- 开始梯度压力测试 ---")
    print(f"压测脚本 PID: {os.getpid()}")
    print(f"URL: {URL}")
    print(f"每轮请求数: {REQUESTS_PER_STEP}")
    print("-" * 30)
    
    results = []
    for concurrency in range(START_CONCURRENCY, MAX_CONCURRENCY + 1, STEP_INCREASE):
        result_step = run_test_step(concurrency)
        results.append(result_step)

    print("\n--- 梯度测试总结 ---")
    print("并发数\t|\tRPS\t|\t平均响应时间 (ms)")
    print("--------|---------------|-------------------")
    for concurrency, rps, avg_time in results:
        print(f"{concurrency}\t|\t{rps:.2f}\t|\t{avg_time:.2f}")

if __name__ == "__main__":
    main()
