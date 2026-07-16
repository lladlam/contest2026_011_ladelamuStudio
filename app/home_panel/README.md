# D13x 家庭中控屏

这是运行在 D13x 衡山派上的 1024x600 横屏 LVGL 原生应用。应用源码通过比赛
manifest 映射到 `packages/demos/contest2026_011_home_panel`；本仓库的
`scripts/integrate.sh` 同时提供不使用 `repo` 工作区时的本地构建映射。

当前应用编译为 NSH builtin：

```text
nsh> home_panel
```

界面直接使用 `/dev/fb0` 的 RGB565 双缓冲和 `/dev/input0` 的 GT911 触摸事件。
账号授权通过有线网络连接独立的 `mijia-api` 服务。点击“登录米家”后，开发板
请求一次性登录会话、显示米家 App 扫码二维码，并在确认后领取短期 Bearer token，
随后同步家庭、房间、设备属性和场景。小米账号 Cookie 等认证数据只保存在服务端
加密保险箱中；开发板仅将服务端 Bearer token 和保险箱解锁口令写入 SPI NOR 的
双槽持久记录，重启后自动解锁并恢复同步。记录采用递增序号和 CRC 校验，写入时
最后提交有效头，避免掉电产生半条有效凭据。

服务地址由 `CONFIG_D13X_HOME_PANEL_MIJIA_SERVER_URL` 配置，当前实机测试默认值为
`http://192.168.1.24:8123`。服务端应与开发板处于同一有线局域网；二维码登录和
设备同步均在独立工作线程中执行，不阻塞 LVGL 刷新和触摸输入。

设备控制按 MIoT-Spec V2 的 `did/siid/piid` 构造 `set_properties`。服务端通过
小米云 HTTP 接口下发，设备侧仍由小米云的 OT 通道完成路由；RPC 接受后再定向
读取属性确认真实值。当前增量接口输出 `properties_changed` 兼容事件，但事件源
是云端回读，并未宣称已经接入官方 OAuth MQTT。后续接入官方 MQTT 时，板端事件
模型无需变化。

应用后台每秒读取 `eth0` 的 carrier 状态，并每 15 秒依次用 ICMP 检测
`mi.com` 与 `xiaomi.cn`。界面区分“有线网络未连接”“无互联网连接”和
“已连接互联网”三种状态；设置页的“重新检测网络”按钮可立即触发一次检测。
DNS 服务器由 DHCP 配置，网络检测运行在独立线程，不阻塞 LVGL 与触摸事件。

SPI NOR 使用 15 MiB 的显式分区布局：系统 3 MiB、只读资源 4 MiB、数据
7 MiB，并预留独立的 256 KiB `userid` 分区。登录凭据双槽位于 `userid`
末尾 8 KiB；该分区不写入升级镜像，因此更新系统、资源或数据分区不会清除登录。

## 开源项目与第三方组件

### 固件运行时依赖

- [OpenVela](https://github.com/open-vela/openvela)：系统工作区、板级集成与应用框架。
- [Apache NuttX](https://github.com/apache/nuttx)：RTOS 内核、NSH、framebuffer、
  I2C 和 touchscreen 子系统，Apache-2.0 许可证。
- [LVGL](https://github.com/lvgl/lvgl)：家庭中控屏图形与事件框架，MIT 许可证；
  当前固件使用 LVGL 9.2.1。
- [cJSON](https://github.com/DaveGamble/cJSON)：解析 `mijia-api` 登录状态与设备
  响应，MIT 许可证；由 OpenVela `NETUTILS_CJSON` 组件集成。
- [ArtInChip Luban-Lite](https://gitee.com/artinchip/luban-lite)：D13x 启动、显示、
  时钟与外设实现的移植参考；其代码不作为本应用的独立运行时库加载。

### 服务端接口参考

- [mijia-api](https://github.com/Do1e/mijia-api)：提供米家 App 扫码登录、家庭、
  设备、属性和场景接口，GPL-3.0 许可证。该项目独立运行在服务器侧，不链接进
  D13x 固件；开发板只通过 HTTP API 与其通信。
- [Xiaomi Home Integration](https://github.com/XiaoMi/ha_xiaomi_home)：小米官方
  Home Assistant 集成，用于核对“HTTP 控制、MQTT 属性/事件订阅”的官方云端
  架构和 MIoT-Spec 消息语义；仅作协议与架构参考，不复制进固件。

### 字体与生成工具

- [MiSans](https://hyperos.mi.com/font/zh/)：界面中文字体。固件只包含当前界面
  所需字符生成的 18px 位图字形，并依据
  [MiSans 字体知识产权许可协议](https://hyperos.mi.com/font-download/MiSans%E5%AD%97%E4%BD%93%E7%9F%A5%E8%AF%86%E4%BA%A7%E6%9D%83%E8%AE%B8%E5%8F%AF%E5%8D%8F%E8%AE%AE.pdf)
  使用和注明；MiSans 本身不是本项目的开源代码。
- [lv_font_conv](https://github.com/lvgl/lv_font_conv)：将本地 MiSans 字体裁剪并
  转换为 LVGL C 字库的构建工具，MIT 许可证；正常固件编译不依赖 Node.js。
