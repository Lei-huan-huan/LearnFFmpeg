# LearnFFmpeg
Learning ffmpeg and video-related projects
<img width="1200" height="2640" alt="7dd396f1d8cae3e8c0234293c6ccbd70" src="https://github.com/user-attachments/assets/c4a2f66c-4571-443c-8cba-1bc2e9679583" />
## 简易播放器说明

这是一个 **简易视频播放器**，支持以下三种视频来源：

- **相册视频**（本地文件）
- **网络视频链接**
- **RTMP 流媒体链接**

---

### 1. 选择文件按钮
- 点击 **选择文件** 按钮后，可以访问系统相册。
- 从相册中选择一个视频后，视频的本地路径会自动填入旁边的输入框。
- 输入框中的内容也可以手动修改，用于输入：
  - 网络视频 URL（如 `https://example.com/video.mp4`）
  - RTMP 链接（如 `rtmp://server/live/stream`）

---

### 2. 播放与停止
- **点击播放**：播放器会根据输入框中的路径或链接，在下方区域开始播放视频。
- **点击停止**：立即停止当前播放。

---

### 3. 支持格式
- 本地相册中的常见视频格式（如 MP4、MOV）
- 网络视频（HTTP/HTTPS）
- RTMP 实时流媒体

---
