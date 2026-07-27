# 启动测试
## 目标

验证 SPI NOR、PBP/tinySPL、NuttX 初始化、驱动注册、NSH 和家庭屏自动启动链路。

## 步骤

1. 记录镜像 SHA-256，完全断电 10 秒后上电，重复 10 次。
2. 每次保存从 tinySPL 第一行开始的完整串口日志。
3. 进入 NSH 后执行 `echo BOOT_OK` 和 `ls /dev`。
4. 使用复位键或受控复位重复 20 次。

## 预期

- 每次均出现 tinySPL 加载 APP、驱动注册和 `NuttShell (NSH)`。
- `/dev` 至少包含 `console`、`ttyS0`、`i2c2`、`fb0` 和 `input0`。
- 家庭屏自动启动且不出现 RISC-V exception、assert 或反复重启。
- 记录最慢启动时间及任何一次启动时序偏差。

## 结果记录

填写启动成功次数、失败次数、平均/最大启动时间，并链接 `serial.log` 和上电视频。
