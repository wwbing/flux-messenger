import asyncio
import json
import struct
import time
import uuid
import sys
from collections import deque

# --- 配置 ---
HOST = '127.0.0.1'
PORT = 8090
CONNECTION_RATE = 200  # 每秒创建多少个新连接
TEST_DURATION = 60  # 总测试时长（秒）
HEARTBEAT_INTERVAL = 10 # 心跳间隔（秒）

# 消息ID定义
MSG_PRESSURE_TEST_REQ = 1025
MSG_PRESSURE_TEST_RSP = 1026
# ---

class TCPClient:
    def __init__(self, uid, active_connections, failed_connections):
        self._uid = uid
        self._reader = None
        self._writer = None
        self._active_connections = active_connections
        self._failed_connections = failed_connections
        self._is_active = False

    def _pack_message(self, msg_id, data):
        """
        打包消息，使用 [2字节ID][2字节长度] 的头部格式
        """
        body = json.dumps(data).encode('utf-8')
        body_len = len(body)
        # >H 表示网络字节序（大端）的 unsigned short (2字节)
        header = struct.pack('>HH', msg_id, body_len)
        return header + body

    async def connect(self):
        try:
            self._reader, self._writer = await asyncio.open_connection(HOST, PORT)
            
            # 发送登录消息
            login_data = {
                "uid": self._uid,
                "token": f"test_token_{self._uid}"
            }
            login_msg = self._pack_message(1005, login_data)
            self._writer.write(login_msg)
            await self._writer.drain()
            
            # 发送初始压测消息
            pressure_data = {"uid": self._uid, "action": "pressure_test"}
            pressure_msg = self._pack_message(MSG_PRESSURE_TEST_REQ, pressure_data)
            self._writer.write(pressure_msg)
            await self._writer.drain()
            
            self._is_active = True
            self._active_connections.add(self)
            
            # 开始心跳循环
            await self._heartbeat_loop()
            
        except Exception as e:
            self._failed_connections.add(self)
            if self._writer:
                self._writer.close()
                await self._writer.wait_closed()

    async def _heartbeat_loop(self):
        try:
            while self._is_active:
                await asyncio.sleep(HEARTBEAT_INTERVAL)
                if not self._is_active:
                    break
                
                # 发送压测心跳消息
                heartbeat_data = {"uid": self._uid, "action": "heartbeat"}
                heartbeat_msg = self._pack_message(MSG_PRESSURE_TEST_REQ, heartbeat_data)
                self._writer.write(heartbeat_msg)
                await self._writer.drain()
                
        except Exception as e:
            self._is_active = False
            if self in self._active_connections:
                self._active_connections.remove(self)
            self._failed_connections.add(self)
            if self._writer:
                self._writer.close()
                await self._writer.wait_closed()

    async def run(self):
        await self.connect()

    async def close(self):
        """关闭连接"""
        self._is_active = False
        if self._writer:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except Exception:
                pass
        
        # 从活跃连接中移除
        if self in self._active_connections:
            self._active_connections.remove(self)


async def main():
    print("重要提示: 在运行此脚本之前，请确保您已经使用 'ulimit -n <number>' (例如 ulimit -n 100000) 提高了操作系统的最大文件描述符限制。")
    input("按 Enter 键继续...")
    
    print(f"开始极限连接数压测，目标服务器: {HOST}:{PORT}")
    print(f"连接速率: {CONNECTION_RATE} conn/s")
    print("-" * 50)
    
    active_connections = set()
    failed_connections = set()
    attempted_count = 0
    
    start_time = time.time()
    
    async def create_connections():
        nonlocal attempted_count
        while time.time() - start_time < TEST_DURATION:
            attempted_count += 1
            client = TCPClient(attempted_count, active_connections, failed_connections)
            asyncio.create_task(client.run())
            await asyncio.sleep(1.0 / CONNECTION_RATE)
    
    async def print_stats():
        while time.time() - start_time < TEST_DURATION:
            elapsed = int(time.time() - start_time)
            active_count = len(active_connections)
            failed_count = len(failed_connections)
            print(f"Time: {elapsed}s | Attempted: {attempted_count} | Active: {active_count} | Failed: {failed_count}")
            await asyncio.sleep(2)
    
    # 运行连接创建和统计任务
    try:
        await asyncio.gather(
            create_connections(),
            print_stats()
        )
    except KeyboardInterrupt:
        print("\n测试被手动中断。")
    
    # 清理所有连接
    cleanup_tasks = []
    for client in active_connections.copy():
        cleanup_tasks.append(asyncio.create_task(client.close()))
    
    if cleanup_tasks:
        await asyncio.gather(*cleanup_tasks, return_exceptions=True)
    
    print(f"\n测试结束。最终统计:")
    print(f"尝试连接数: {attempted_count}")
    print(f"成功连接数: {len(active_connections)}")
    print(f"失败连接数: {len(failed_connections)}")

if __name__ == '__main__':

    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n测试被手动中断。")
