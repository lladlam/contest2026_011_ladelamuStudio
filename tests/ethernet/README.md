# 以太网测试
## 基本连通

```text
nsh> renew eth0
nsh> ifconfig
nsh> ping 192.168.1.1
```

同时在同网段电脑上运行 `tcpdump -ni <interface> -e -vvv -XX`，保存 DHCP、ARP、
ICMP 和 DNS 证据。

## 断线恢复

1. 获取 DHCP 地址并确认家庭屏显示“已连接互联网”。
2. 拔掉网线，确认显示“有线网络未连接”。
3. 重新插入，确认 PHY link up、DHCP/DNS 恢复和“已连接互联网”。
4. 重复 20 次；另执行 20 次 DHCP 重新获取。

## 预期

- 100M 全双工协商稳定，DHCP 报文格式正确。
- 网线、局域网和互联网三种状态区分准确。
- 断线期间 UI 继续响应，恢复后米家增量同步自动继续。
- 不出现错误长度帧、描述符卡死、永久 `checking` 或串口不可输入。
