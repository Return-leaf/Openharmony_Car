Page({
  data: {
    connected: false,
    serverIp: '',
    stickStyle: '',
    speedStyle: 'width: 70%;',
    gear: 180,
    driftMode: false,
    lastCmdName: '停止',
    sonarFront: '-',
    sonarBack: '-',
    sonarLeft: '-',
    sonarRight: '-'
  },

  socket: null,
  rect: null,
  heartbeatTimer: null,
  currentCmd: 'stop:0',

  onLoad() {
    const savedIp = wx.getStorageSync('car_server_ip');
    if (savedIp) this.setData({ serverIp: savedIp });
  },

  onReady() { this.initCoordinates(); this.initSocket(); },

  /* ==================== WebSocket ==================== */

  initSocket() {
    const ip = this.data.serverIp;
    if (!ip) { this.setData({ connected: false }); return; }
    this.socket = wx.connectSocket({ url: `ws://${ip}:8080` });
    this.socket.onOpen(() => this.setData({ connected: true }));
    this.socket.onMessage((res) => {
      try {
        const d = JSON.parse(res.data);
        if (d.type === 'sonar') {
          this.setData({
            sonarFront: d.front || '-',
            sonarBack: d.back || '-',
            sonarLeft: d.left || '-',
            sonarRight: d.right || '-'
          });
        }
      } catch (e) {}
    });
    this.socket.onClose(() => {
      this.setData({ connected: false });
      setTimeout(() => this.initSocket(), 3000);
    });
    this.socket.onError(() => {
      this.setData({ connected: false });
      setTimeout(() => this.initSocket(), 3000);
    });
  },

  sendMsg(dir, speed) {
    if (this.data.connected) {
      this.socket.send({ data: `${dir}:${speed}` });
    }
  },

  /* ==================== IP 配置 ==================== */

  showSettings() {
    wx.showModal({
      title: '设置小车IP地址',
      editable: true,
      placeholderText: '例: 192.168.43.100',
      content: this.data.serverIp || '',
      success: (res) => {
        if (res.confirm && res.content) {
          const ip = res.content.trim();
          this.setData({ serverIp: ip });
          wx.setStorageSync('car_server_ip', ip);
          if (this.socket) this.socket.close({ code: 1000 });
          setTimeout(() => this.initSocket(), 500);
        }
      }
    });
  },

  /* ==================== 漂移 / 挡位 ==================== */

  toggleDrift() {
    this.setData({ driftMode: !this.data.driftMode });
    wx.vibrateLong();
  },

  setGear(e) {
    const val = parseInt(e.currentTarget.dataset.val);
    this.setData({
      gear: val,
      speedStyle: `width: ${Math.floor(val/2.55)}%;`
    });
    wx.vibrateShort({ type: 'medium' });
  },

  /* ==================== 摇杆 ==================== */

  initCoordinates() {
    wx.createSelectorQuery().select('#base').boundingClientRect(rect => {
      if (rect) {
        this.rect = rect;
        const centerX = rect.width / 2;
        const stickS = rect.width * 120 / 320;
        const p = centerX - (stickS / 2);
        this.setData({ stickStyle: `left:${p}px; top:${p}px;` });
      }
    }).exec();
  },

  touchMove(e) {
    if (!this.rect) return;
    const touch = e.touches[0];
    const centerX = this.rect.width / 2;
    const centerY = this.rect.height / 2;
    const stickS = this.rect.width * 120 / 320;
    let dx = touch.clientX - this.rect.left - centerX;
    let dy = touch.clientY - this.rect.top - centerY;
    const dist = Math.sqrt(dx * dx + dy * dy);
    const maxR = centerX - 10;

    if (dist > maxR) { dx *= (maxR / dist); dy *= (maxR / dist); }

    this.setData({
      stickStyle: `left:${centerX + dx - (stickS/2)}px; top:${centerY + dy - (stickS/2)}px;`
    });

    if (dist < 30) {
      this.doSend('stop');
    } else {
      const angle = Math.atan2(dy, dx) * 180 / Math.PI;
      let dir = '';
      if (angle > -45 && angle <= 45) dir = 'right';
      else if (angle > 45 && angle <= 135) dir = 'backward';
      else if (angle > 135 || angle <= -135) dir = 'left';
      else dir = 'forward';
      this.doSend(dir);
    }

    /* 启动心跳：手指按住不动时 touchMove 不会触发，心跳每 300ms 重发保证不丢指令 */
    if (!this.heartbeatTimer) {
      this.heartbeatTimer = setInterval(() => {
        if (this.data.connected && this.currentCmd) {
          this.socket.send({ data: this.currentCmd });
        }
      }, 300);
    }
  },

  doSend(dir) {
    let finalDir = dir;
    let speed = (dir === 'stop') ? 0 : this.data.gear;

    if (this.data.driftMode && (dir === 'left' || dir === 'right')) {
      speed = 255;
      finalDir = (dir === 'left') ? 'drift_l' : 'drift_r';
    }

    this.currentCmd = `${finalDir}:${speed}`;
    this.sendMsg(finalDir, speed);
    this.setData({ lastCmdName: finalDir });
    wx.vibrateShort({ type: 'light' });
  },

  touchEnd() {
    if (this.heartbeatTimer) { clearInterval(this.heartbeatTimer); this.heartbeatTimer = null; }
    this.currentCmd = 'stop:0';
    this.initCoordinates();
    this.sendMsg('stop', 0);
    this.setData({ lastCmdName: '停止' });
  }
});
