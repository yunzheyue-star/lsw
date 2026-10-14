# lswd IPC 接口

传输：Unix Domain Socket  
路径：`$XDG_RUNTIME_DIR/lsw/lswd.sock`  
格式：JSON Lines

请求：

```json
{ "id": "1", "cmd": "start", "args": {} }
```

响应：

```json
{ "id": "1", "ok": true, "data": {} }
```

命令：
- `start`
- `shutdown`
- `shell`
- `exec`
- `status`
- `mount`
- `unmount`