"""Local Windows tkinter panel. No web server or third-party dependencies."""
from pathlib import Path
import queue
import secrets
import threading
import time
import tkinter as tk
from tkinter import ttk
from dual_test import run


class Panel:
    def __init__(self, root):
        self.root = root
        root.title('IGW Linux CAN + RS485 → UDP')
        root.geometry('960x620')
        self.events = queue.Queue()
        self.stop = threading.Event()
        self.running = self.closing = False
        self.totals = {}
        self.host = tk.StringVar(value='192.168.31.222')
        self.count = tk.StringVar(value='10')
        self.mode = tk.StringVar(value='双协议')
        top = ttk.Frame(root, padding=12)
        top.pack(fill='x')
        for index, (label, var, width) in enumerate([
            ('主机 IP', self.host, 18), ('每通道次数', self.count, 8)]):
            ttk.Label(top, text=label).grid(row=0, column=index*2)
            ttk.Entry(top, textvariable=var, width=width).grid(row=0, column=index*2+1, padx=5)
        ttk.Combobox(top, textvariable=self.mode, values=['双协议', '仅 CAN', '仅 RS485'],
                     state='readonly', width=12).grid(row=0, column=4, padx=5)
        self.button = ttk.Button(top, text='开始测试', command=self.start)
        self.button.grid(row=0, column=5, padx=5)
        ttk.Button(top, text='停止', command=self.stop.set).grid(row=0, column=6)
        self.status = tk.StringVar(value='先在主机启动 run_dual_gateway.sh；两通道每秒各发起一次请求。')
        ttk.Label(root, textvariable=self.status, padding=12, wraplength=900).pack(fill='x')
        self.metrics = tk.StringVar(value='CAN：0/0　RS485：0/0')
        ttk.Label(root, textvariable=self.metrics, padding=8).pack(fill='x')
        self.log = tk.Text(root, height=24, wrap='word')
        self.log.pack(fill='both', expand=True, padx=12, pady=8)
        ttk.Label(root, text='PC RTT 包含网络往返；总线 RTT 包含请求、从机处理、回复及软件轮询。', padding=8).pack(fill='x')
        root.protocol('WM_DELETE_WINDOW', self.close)
        root.after(100, self.pump)

    def start(self):
        if self.running:
            return
        try:
            count = int(self.count.get())
            if not 1 <= count <= 1000000:
                raise ValueError()
        except ValueError:
            self.status.set('次数请输入 1～1000000。')
            return
        channels = {'双协议': ['CAN', 'RS485'], '仅 CAN': ['CAN'], '仅 RS485': ['RS485']}[self.mode.get()]
        host = self.host.get().strip()
        self.totals = {ch: [0, 0] for ch in channels}
        self.stop.clear()
        self.running = True
        self.button.configure(state='disabled')
        self.status.set('测试中；停止会等待当前请求返回，最多约 2 秒。')
        threading.Thread(target=self.worker, args=(host, count, channels), daemon=False).start()

    def worker(self, host, count, channels):
        path = Path(f'igw_dual_{time.strftime("%Y%m%d_%H%M%S")}_{secrets.token_hex(2)}.csv').resolve()
        try:
            run(host, count, channels, self.stop, lambda r: self.events.put(('result', r)), path)
        except Exception as exc:
            self.events.put(('error', str(exc)))
        finally:
            self.events.put(('done', str(path)))

    def pump(self):
        try:
            while True:
                kind, value = self.events.get_nowait()
                if kind == 'result':
                    total = self.totals[value['channel']]
                    total[0] += 1
                    total[1] += value.get('status') == 'ok'
                    self.metrics.set('　'.join(f'{ch}：成功 {n[1]}/{n[0]}' for ch, n in self.totals.items()))
                    self.log.insert('end', f"{value['channel']} seq={value['seq']} {value.get('status')} "
                                    f"PC={value.get('pc_rtt_ms')}ms 总线={value.get('bus_rtt_us')}us "
                                    f"寄存器={value.get('registers', '')}\n")
                elif kind == 'error':
                    self.log.insert('end', f'错误：{value}\n')
                elif kind == 'done':
                    self.running = False
                    self.button.configure(state='normal')
                    self.status.set(f'结束。CSV：{value}')
                self.log.see('end')
        except queue.Empty:
            pass
        if self.closing and not self.running:
            self.root.destroy()
        else:
            self.root.after(100, self.pump)

    def close(self):
        self.closing = True
        self.stop.set()
        if not self.running:
            self.root.destroy()


if __name__ == '__main__':
    root = tk.Tk()
    Panel(root)
    root.mainloop()
