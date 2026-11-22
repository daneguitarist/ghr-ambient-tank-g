// TANK-G FLASH - installer / restorer for GHR AMBIENT, custom firmware for the M-VAVE Tank-G v2 (GerbilHamster Records).
// SPDX-License-Identifier: GPL-3.0-only
//   BACKUP:  reads the pedal's whole flash twice, checks the copies match and that it holds a complete factory
//            firmware, and saves it as backup_<date>\flash_full_1MiB.bin with its fingerprint. Writes nothing.
//   INSTALL: makes that backup first if there isn't one, then puts our firmware into the firmware area. The firmware
//            area is built here, on your computer, from our app (ghr_ambient_app.bin) and your own pedal's backup, so
//            nothing of M-VAVE's is ever shipped with this tool.
//   RESTORE: puts the factory firmware back from your backup.
//   STATUS:  reads our firmware's report (levels, CPU, what happened). Writes nothing.
//
// Only the firmware area, flash 0x4000-0x7AFFF, can ever be erased or written (Guard), and every write must be
// exactly the planned bytes for that sector (GuardData). The bootloader (0x0000-0x3FFF), the settings area, the
// amps/cabs and everything else are only read, and checked against your backup before anything is written.
// Each sector is erased, written, and read back; then the whole flash is read and checked again.
//
// Getting into the loader: if the factory firmware is running, the same MIDI message M-VAVE's own updater uses.
// If our firmware is running: the same message over USB, or hold footswitch A while plugging the pedal in, or
// hold A + D for 2 s. The loader runs JieLi's small RAM loader (wl82loader.bin, from kagaimiq's jl-uboot-tool,
// fetched and checked by fingerprint - it is not part of this tool).

using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

public static class TankFlash {
  // ------------------------------------------------------------------ logging
  static StringBuilder log = new StringBuilder();
  static string logPath;      // report.txt grows line by line, so closing the window early still leaves a report
  public static void L(string s) {
    string line = DateTime.Now.ToString("HH:mm:ss.fff") + "  " + s;
    log.AppendLine(line);
    Console.WriteLine(s);
    if (logPath != null) { try { File.AppendAllText(logPath, line + "\r\n"); } catch (Exception) { } }
  }
  static string Hex(byte[] b, int off, int n) {
    var s = new StringBuilder();
    for (int i = 0; i < n; i++) { if (i > 0) s.Append(' '); s.Append(b[off + i].ToString("X2")); }
    return s.ToString();
  }

  // ------------------------------------------------------------------ MIDI (winmm)
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  struct MIDIINCAPS { public ushort wMid, wPid; public uint vDriverVersion;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string szPname; public uint dwSupport; }
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  struct MIDIOUTCAPS { public ushort wMid, wPid; public uint vDriverVersion;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string szPname;
    public ushort wTechnology, wVoices, wNotes, wChannelMask; public uint dwSupport; }
  [StructLayout(LayoutKind.Sequential)]
  struct MIDIHDR { public IntPtr lpData; public uint dwBufferLength; public uint dwBytesRecorded;
    public IntPtr dwUser; public uint dwFlags; public IntPtr lpNext; public IntPtr reserved; public uint dwOffset;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)] public IntPtr[] dwReserved; }

  [DllImport("winmm.dll")] static extern uint midiInGetNumDevs();
  [DllImport("winmm.dll")] static extern uint midiOutGetNumDevs();
  [DllImport("winmm.dll", CharSet = CharSet.Unicode)] static extern uint midiInGetDevCapsW(UIntPtr id, ref MIDIINCAPS c, uint sz);
  [DllImport("winmm.dll", CharSet = CharSet.Unicode)] static extern uint midiOutGetDevCapsW(UIntPtr id, ref MIDIOUTCAPS c, uint sz);
  [DllImport("winmm.dll")] static extern uint midiInOpen(out IntPtr h, uint id, IntPtr cb, IntPtr inst, uint flags);
  [DllImport("winmm.dll")] static extern uint midiInPrepareHeader(IntPtr h, IntPtr hdr, uint sz);
  [DllImport("winmm.dll")] static extern uint midiInAddBuffer(IntPtr h, IntPtr hdr, uint sz);
  [DllImport("winmm.dll")] static extern uint midiInUnprepareHeader(IntPtr h, IntPtr hdr, uint sz);
  [DllImport("winmm.dll")] static extern uint midiInStart(IntPtr h);
  [DllImport("winmm.dll")] static extern uint midiInStop(IntPtr h);
  [DllImport("winmm.dll")] static extern uint midiInReset(IntPtr h);
  [DllImport("winmm.dll")] static extern uint midiInClose(IntPtr h);
  [DllImport("winmm.dll")] static extern uint midiOutOpen(out IntPtr h, uint id, IntPtr cb, IntPtr inst, uint flags);
  [DllImport("winmm.dll")] static extern uint midiOutPrepareHeader(IntPtr h, IntPtr hdr, uint sz);
  [DllImport("winmm.dll")] static extern uint midiOutUnprepareHeader(IntPtr h, IntPtr hdr, uint sz);
  [DllImport("winmm.dll")] static extern uint midiOutLongMsg(IntPtr h, IntPtr hdr, uint sz);
  [DllImport("winmm.dll")] static extern uint midiOutClose(IntPtr h);
  const uint MHDR_DONE = 1;

  static IntPtr NewHdr(IntPtr data, int len) {
    var h = new MIDIHDR(); h.lpData = data; h.dwBufferLength = (uint)len; h.dwReserved = new IntPtr[8];
    IntPtr p = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(MIDIHDR)));
    Marshal.StructureToPtr(h, p, false); return p;
  }
  static int Off(string f) { return (int)Marshal.OffsetOf(typeof(MIDIHDR), f); }

  static void FindPorts(out int inId, out int outId) {
    inId = -1; outId = -1;
    for (uint i = 0; i < midiInGetNumDevs(); i++) {
      var c = new MIDIINCAPS(); midiInGetDevCapsW((UIntPtr)i, ref c, (uint)Marshal.SizeOf(typeof(MIDIINCAPS)));
      if (inId < 0 && (c.szPname == "USB-Midi" || c.szPname.Contains("Tank-G GHR"))) inId = (int)i;
    }
    for (uint i = 0; i < midiOutGetNumDevs(); i++) {
      var c = new MIDIOUTCAPS(); midiOutGetDevCapsW((UIntPtr)i, ref c, (uint)Marshal.SizeOf(typeof(MIDIOUTCAPS)));
      if (outId < 0 && (c.szPname == "USB-Midi" || c.szPname.Contains("Tank-G GHR"))) outId = (int)i;
    }
  }

  // Sends one SysEx, collects SysEx replies for waitMs. Returns null if the ports are missing/busy.
  static List<byte[]> SysEx(byte[] msg, int waitMs) {
    int inId, outId; FindPorts(out inId, out outId);
    if (inId < 0 || outId < 0) return null;
    uint hsz = (uint)Marshal.SizeOf(typeof(MIDIHDR));
    var got = new List<byte[]>();
    IntPtr hin, hout;
    if (midiInOpen(out hin, (uint)inId, IntPtr.Zero, IntPtr.Zero, 0) != 0) return null;
    var bufs = new IntPtr[4]; var hdrs = new IntPtr[4];
    for (int i = 0; i < 4; i++) {
      bufs[i] = Marshal.AllocHGlobal(1024); hdrs[i] = NewHdr(bufs[i], 1024);
      midiInPrepareHeader(hin, hdrs[i], hsz); midiInAddBuffer(hin, hdrs[i], hsz);
    }
    midiInStart(hin);
    if (midiOutOpen(out hout, (uint)outId, IntPtr.Zero, IntPtr.Zero, 0) == 0) {
      IntPtr q = Marshal.AllocHGlobal(msg.Length); Marshal.Copy(msg, 0, q, msg.Length);
      IntPtr qh = NewHdr(q, msg.Length);
      midiOutPrepareHeader(hout, qh, hsz);
      midiOutLongMsg(hout, qh, hsz);
      for (int t = 0; t < 100 && (Marshal.ReadInt32(qh, Off("dwFlags")) & MHDR_DONE) == 0; t++) Thread.Sleep(10);
      var until = DateTime.Now.AddMilliseconds(waitMs);
      while (DateTime.Now < until) {
        for (int i = 0; i < 4; i++) {
          int fl = Marshal.ReadInt32(hdrs[i], Off("dwFlags"));
          if ((fl & MHDR_DONE) != 0) {
            int n = Marshal.ReadInt32(hdrs[i], Off("dwBytesRecorded"));
            if (n > 0) { var b = new byte[n]; Marshal.Copy(bufs[i], b, 0, n); got.Add(b); }
            Marshal.WriteInt32(hdrs[i], Off("dwBytesRecorded"), 0);
            Marshal.WriteInt32(hdrs[i], Off("dwFlags"), fl & ~(int)MHDR_DONE);
            midiInAddBuffer(hin, hdrs[i], hsz);
          }
        }
        if (got.Count > 0 && waitMs < 5000) break;
        Thread.Sleep(10);
      }
      midiOutUnprepareHeader(hout, qh, hsz); midiOutClose(hout);
      Marshal.FreeHGlobal(qh); Marshal.FreeHGlobal(q);
    }
    midiInStop(hin); midiInReset(hin);
    for (int i = 0; i < 4; i++) { midiInUnprepareHeader(hin, hdrs[i], hsz); Marshal.FreeHGlobal(hdrs[i]); Marshal.FreeHGlobal(bufs[i]); }
    midiInClose(hin);
    return got;
  }

  // 8-to-7 bit unpack (LSB first), as in the M-VAVE updater protocol.
  static byte[] Unpack7(byte[] s, int off, int n) {
    var o = new List<byte>(); int acc = 0, nb = 0;
    for (int i = 0; i < n; i++) { acc |= s[off + i] << nb; nb += 7; while (nb >= 8) { o.Add((byte)(acc & 0xFF)); acc >>= 8; nb -= 8; } }
    return o.ToArray();
  }

  static string Identify() {
    var q = new byte[] { 0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7 };
    for (int a = 0; a < 3; a++) {
      var r = SysEx(q, 1500);
      if (r == null) return null;
      foreach (var p in r) {
        if (p.Length < 10 || p[0] != 0xF0 || p[p.Length - 1] != 0xF7) continue;
        var d = Unpack7(p, 1, p.Length - 2);
        if (d.Length != 34 || d[0] != 0x00 || d[1] != 0x59 || d[2] != 0x11) continue;
        int sum = 0; for (int i = 6; i < 33; i++) sum += d[i];
        if (d[33] != (byte)(~sum)) continue;
        int end = 6; while (end < 33 && d[end] != 0) end++;
        return Encoding.ASCII.GetString(d, 6, end - 6);
      }
    }
    return "";
  }

  // ------------------------------------------------------------------ SCSI pass-through
  [StructLayout(LayoutKind.Sequential)]
  struct SPTD {
    public ushort Length; public byte ScsiStatus, PathId, TargetId, Lun, CdbLength, SenseInfoLength, DataIn;
    public uint DataTransferLength, TimeOutValue; public IntPtr DataBuffer; public uint SenseInfoOffset;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)] public byte[] Cdb;
  }
  [StructLayout(LayoutKind.Sequential)]
  struct SPTDWB { public SPTD s; public uint Filler; [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)] public byte[] Sense; }

  [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
  static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sa, uint disp, uint flags, IntPtr tmpl);
  [DllImport("kernel32.dll", SetLastError = true)] static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll", SetLastError = true)]
  static extern bool DeviceIoControl(IntPtr h, uint code, ref SPTDWB inb, uint inSz, ref SPTDWB outb, uint outSz, out uint ret, IntPtr ov);
  [DllImport("kernel32.dll")] static extern uint GetLogicalDrives();
  const uint IOCTL_SCSI_PASS_THROUGH_DIRECT = 0x4D014;
  static readonly IntPtr INVALID = new IntPtr(-1);

  static IntPtr dev = INVALID; static string devPath = null;

  static void Open(string path) {
    IntPtr h = CreateFileW(path, 0xC0000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
    if (h == INVALID) throw new IOException("open " + path + " failed, error " + Marshal.GetLastWin32Error());
    dev = h; devPath = path;
  }
  static void Close() { if (dev != INVALID) { CloseHandle(dev); dev = INVALID; } }

  // dir: 0 = out, 1 = in, 2 = none
  static void Exec(byte[] cdb, byte[] data, int dir, int timeoutS) {
    var w = new SPTDWB(); w.s.Cdb = new byte[16]; w.Sense = new byte[32];
    Array.Copy(cdb, w.s.Cdb, cdb.Length);
    w.s.Length = (ushort)Marshal.SizeOf(typeof(SPTD));
    w.s.PathId = 0; w.s.TargetId = 1; w.s.Lun = 0;
    w.s.CdbLength = (byte)cdb.Length; w.s.SenseInfoLength = 32;
    w.s.SenseInfoOffset = (uint)Marshal.OffsetOf(typeof(SPTDWB), "Sense");
    w.s.TimeOutValue = (uint)timeoutS;
    IntPtr buf = IntPtr.Zero;
    try {
      if (data != null && data.Length > 0) {
        buf = Marshal.AllocHGlobal(data.Length);
        if (dir == 0) Marshal.Copy(data, 0, buf, data.Length);
        w.s.DataBuffer = buf; w.s.DataTransferLength = (uint)data.Length; w.s.DataIn = (byte)dir;
      } else { w.s.DataIn = 2; }
      uint ret; uint sz = (uint)Marshal.SizeOf(typeof(SPTDWB));
      if (!DeviceIoControl(dev, IOCTL_SCSI_PASS_THROUGH_DIRECT, ref w, sz, ref w, sz, out ret, IntPtr.Zero))
        throw new IOException("transfer failed, error " + Marshal.GetLastWin32Error());
      if (w.s.ScsiStatus != 0) throw new IOException("SCSI status " + w.s.ScsiStatus);
      if (dir == 1 && data != null) Marshal.Copy(buf, data, 0, data.Length);
    } finally { if (buf != IntPtr.Zero) Marshal.FreeHGlobal(buf); }
  }

  static string[] Inquiry() {
    var d = new byte[36];
    Exec(new byte[] { 0x12, 0, 0, 0, 36, 0 }, d, 1, 5);
    return new string[] { Encoding.ASCII.GetString(d, 8, 8).Trim(), Encoding.ASCII.GetString(d, 16, 16).Trim(), Encoding.ASCII.GetString(d, 32, 4).Trim() };
  }

  // ------------------------------------------------------------------ JieLi commands (whitelisted)
  const uint LOADER_ADDR = 0x1C02000;
  const int SECTOR = 0x1000;
  const int SIZE = 0x100000;
  public const uint AREA_LO = 0x4000, AREA_HI = 0x7B000;   // the app area: the only part this tool may change
  const uint VM_LO = 0x7B000, VM_HI = 0xBD000;             // the settings area (changes as the pedal is used)
  const uint STAT_ADDR = 0x1C3F000;                         // our firmware's status block (RAM)
  static int loaderLen = 0;
  static bool flashTouched = false;
  // what each app-area sector must end up as: writes are only allowed with exactly these bytes
  public static byte[] target;
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern uint GetDriveTypeW(string root);

  static void Guard(int cmd, uint addr, int len) {
    switch (cmd) {
      case 0xFB06:
        if (addr < LOADER_ADDR || addr + (uint)len > LOADER_ADDR + (uint)loaderLen) throw new Exception("blocked RAM write outside loader area");
        return;
      case 0xFB08: if (addr != LOADER_ADDR) throw new Exception("blocked jump"); return;
      case 0xFD07: if (addr < STAT_ADDR || addr + (uint)len > STAT_ADDR + 0x400) throw new Exception("blocked RAM read"); return;
      case 0xFB01:
        if ((addr & (SECTOR - 1)) != 0 || addr < AREA_LO || addr >= AREA_HI) throw new Exception("blocked erase at 0x" + addr.ToString("X6"));
        return;
      case 0xFB04:
        if (len != SECTOR || (addr & (SECTOR - 1)) != 0 || addr < AREA_LO || addr >= AREA_HI || target == null)
          throw new Exception("blocked write at 0x" + addr.ToString("X6"));
        return;
      case 0xFC09: case 0xFC0A: case 0xFC0B: case 0xFC14: case 0xFC15: case 0xFD05: case 0xFC0C: return;
      default: throw new Exception("blocked command " + cmd.ToString("X4"));
    }
  }
  static void GuardData(uint addr, byte[] data) {
    for (int i = 0; i < SECTOR; i++) if (data[i] != target[addr + i]) throw new Exception("blocked write at 0x" + addr.ToString("X6") + " (not the planned bytes)");
  }
  static byte[] Cdb(int cmd, byte[] args) {
    var c = new byte[16]; for (int i = 0; i < 16; i++) c[i] = 0xFF;
    c[0] = (byte)(cmd >> 8); c[1] = (byte)cmd; Array.Copy(args, 0, c, 2, args.Length); return c;
  }
  static byte[] BE(uint v, int n) { var b = new byte[n]; for (int i = 0; i < n; i++) b[i] = (byte)(v >> (8 * (n - 1 - i))); return b; }
  static byte[] Cat(params byte[][] parts) { var o = new List<byte>(); foreach (var p in parts) o.AddRange(p); return o.ToArray(); }
  static byte[] Cmd(int cmd, byte[] args, bool check) {
    Guard(cmd, args.Length >= 4 ? (uint)((args[0] << 24) | (args[1] << 16) | (args[2] << 8) | args[3]) : 0, 0);
    var r = new byte[16];
    Exec(Cdb(cmd, args), r, 1, 15);
    if (check && ((r[0] << 8) | r[1]) != cmd) throw new IOException("bad reply to " + cmd.ToString("X4") + ": " + Hex(r, 0, 16));
    var o = new byte[14]; Array.Copy(r, 2, o, 0, 14); return o;
  }
  static ushort Crc16(byte[] d, ushort crc) {
    foreach (byte b in d) { crc ^= (ushort)(b << 8); for (int i = 0; i < 8; i++) crc = (ushort)((crc & 0x8000) != 0 ? (crc << 1) ^ 0x1021 : crc << 1); }
    return crc;
  }
  static byte[] MengLi(byte[] blk) {
    byte[] magic = new byte[] { 0xC3, 0xCF, 0xC0, 0xE8, 0xCE, 0xD2, 0xB0, 0xAE, 0xC4, 0xE3, 0xA3, 0xAC, 0xD3, 0xF1, 0xC1, 0xD6 };
    var o = (byte[])blk.Clone();
    ushort crc = Crc16(new byte[] { 0xFF, 0xFF }, 0xFFFF);
    for (int i = 0; i < o.Length; i++) { crc = Crc16(new byte[] { magic[i % 16] }, crc); o[i] ^= (byte)crc; }
    return o;
  }
  static void MemWrite(uint addr, byte[] data) {
    Guard(0xFB06, addr, data.Length);
    ushort crc = Crc16(data, 0);
    var args = Cat(BE(addr, 4), BE((uint)data.Length, 2), new byte[] { 0, (byte)crc, (byte)(crc >> 8) });
    Exec(Cdb(0xFB06, args), data, 0, 10);
  }
  static byte[] MemRead(uint addr, int len) {
    Guard(0xFD07, addr, len);
    var d = new byte[len];
    Exec(Cdb(0xFD07, Cat(BE(addr, 4), BE((uint)len, 2))), d, 1, 5);
    return d;
  }
  static int ChipKey() {
    var r = Cmd(0xFC09, BE(0xAC6900, 4), true);
    byte[] k = new byte[] { r[5], r[4] };
    byte[] magic = new byte[] { 0xC3, 0xCF, 0xC0, 0xE8, 0xCE, 0xD2, 0xB0, 0xAE, 0xC4, 0xE3, 0xA3, 0xAC, 0xD3, 0xF1, 0xC1, 0xD6 };
    ushort crc = Crc16(new byte[] { 0xFF, 0xFF }, 0xFFFF);
    for (int i = 0; i < k.Length; i++) { crc = Crc16(new byte[] { magic[i % 16] }, crc); k[i] ^= (byte)crc; }
    return k[0] | (k[1] << 8);
  }

  // ------------------------------------------------------------------ the firmware area's format
  // Written from our own decode of the pedal's flash (JieLi's directory format and SFC cipher, as documented by
  // kagaimiq): the area is enciphered in 32-byte blocks, each with key ^ (offset >> 2) run through CRC-16 steps.
  // Plain area: 0x000 app_area_head, 0x020 app.bin, 0x040 cfg_tool.bin, 0x060..0x0FF five fixed entries (settings,
  // protect, BT info, OTP, user), 0x120 app.bin's data, cfg_tool.bin's data right after it, and right after that a
  // small nested directory "cfg". Each entry: hcrc, dcrc, offset, size, attr, r, idx, 16-byte name (CRC-16/XMODEM).
  static ushort Crc16(byte[] d, int off, int n, ushort crc) {
    for (int j = 0; j < n; j++) { crc ^= (ushort)(d[off + j] << 8); for (int i = 0; i < 8; i++) crc = (ushort)((crc & 0x8000) != 0 ? (crc << 1) ^ 0x1021 : crc << 1); }
    return crc;
  }
  // en/decipher len bytes of data[off..] that sit at area offset addr (addr and the blocks counted from area offset 0)
  static byte[] Sfc(byte[] data, int off, int len, int key, int addr) {
    var o = new byte[len];
    for (int b = 0; b < len; b += 32) {
      int k = (key ^ ((addr + b) >> 2)) & 0xFFFF;
      int n = Math.Min(32, len - b);
      for (int i = 0; i < n; i++) { o[b + i] = (byte)(data[off + b + i] ^ (k & 0xFF)); k = ((k << 1) ^ ((k >> 15) != 0 ? 0x1021 : 0)) & 0xFFFF; }
    }
    return o;
  }
  public class Ent { public ushort dcrc; public uint off, size; public byte attr, r; public ushort idx; public string name; }
  static Ent ParseEnt(byte[] w, int o) {
    var e = new Ent();
    e.dcrc = BitConverter.ToUInt16(w, o + 2); e.off = BitConverter.ToUInt32(w, o + 4); e.size = BitConverter.ToUInt32(w, o + 8);
    e.attr = w[o + 12]; e.r = w[o + 13]; e.idx = BitConverter.ToUInt16(w, o + 14);
    int n = 0; while (n < 16 && w[o + 16 + n] != 0 && w[o + 16 + n] != 0xFF) n++;
    e.name = Encoding.ASCII.GetString(w, o + 16, n);
    return e;
  }
  static bool EntOk(byte[] w, int o) { return BitConverter.ToUInt16(w, o) == Crc16(w, o + 2, 30, 0); }
  static void PutEnt(byte[] w, int o, ushort dcrc, uint off, uint size, Ent like) {
    var b = new byte[32];
    BitConverter.GetBytes(dcrc).CopyTo(b, 2); BitConverter.GetBytes(off).CopyTo(b, 4); BitConverter.GetBytes(size).CopyTo(b, 8);
    b[12] = like.attr; b[13] = like.r; BitConverter.GetBytes(like.idx).CopyTo(b, 14);
    for (int i = 16; i < 32; i++) b[i] = 0xFF;
    var nb = Encoding.ASCII.GetBytes(like.name); Array.Copy(nb, 0, b, 16, nb.Length); b[16 + nb.Length] = 0;
    BitConverter.GetBytes(Crc16(b, 2, 30, 0)).CopyTo(b, 0);
    Array.Copy(b, 0, w, o, 32);
  }
  static int Find(byte[] d, int off, int n, string s) {
    var p = Encoding.ASCII.GetBytes(s);
    for (int i = off; i + p.Length <= off + n; i++) { int j = 0; while (j < p.Length && d[i + j] == p[j]) j++; if (j == p.Length) return i; }
    return -1;
  }

  // The key the area is enciphered with, found from the image itself (the entry at offset 0 must decode with a good
  // CRC as app_area_head). -1: none, -2: more than one.
  public static int FindKey(byte[] flash) {
    int found = -1;
    for (int k = 0; k < 0x10000; k++) {
      var w = Sfc(flash, (int)AREA_LO, 32, k, 0);
      if (w[16] != (byte)'a' || w[28] != (byte)'d' || !EntOk(w, 0) || ParseEnt(w, 0).name != "app_area_head") continue;
      if (found >= 0) return -2;
      found = k;
    }
    return found;
  }

  public class Layout { public int key; public Ent head, app, cfg; public byte[] plain; public int cfgDir, cfgDirLen, areaLen; public bool ours; public string problem; }
  // Decode and check the firmware area of a whole-flash image. problem == null: it is complete and consistent.
  public static Layout ReadLayout(byte[] flash, int key) {
    var F = new Layout(); F.key = key;
    if (flash.Length != SIZE) { F.problem = "the image isn't 1 MiB"; return F; }
    var w = Sfc(flash, (int)AREA_LO, (int)(AREA_HI - AREA_LO), key, 0);
    F.plain = w;
    for (int i = 0; i < 8; i++) if (!EntOk(w, 32 * i)) { F.problem = "directory entry " + i + " doesn't check out"; return F; }
    F.head = ParseEnt(w, 0); F.app = ParseEnt(w, 32); F.cfg = ParseEnt(w, 64);
    if (F.head.name != "app_area_head" || F.app.name != "app.bin" || F.cfg.name != "cfg_tool.bin") { F.problem = "unexpected directory: " + F.head.name + ", " + F.app.name + ", " + F.cfg.name; return F; }
    if (F.head.off != 0x02000120 || F.app.off != 0x120 || F.cfg.off != F.app.off + F.app.size || F.head.size != F.cfg.off + F.cfg.size || F.app.size < 0x1000 || F.cfg.size == 0 || F.head.size + 32 > w.Length) { F.problem = "unexpected layout"; return F; }
    if (F.head.dcrc != Crc16(w, 0x20, (int)F.head.size - 0x20, 0)) { F.problem = "the firmware area's checksum doesn't match (an unfinished install?)"; return F; }
    if (F.app.dcrc != Crc16(w, (int)F.app.off, (int)F.app.size, 0) || F.cfg.dcrc != Crc16(w, (int)F.cfg.off, (int)F.cfg.size, 0)) { F.problem = "a file's checksum doesn't match"; return F; }
    int d = (int)F.head.size;
    uint dsize = BitConverter.ToUInt32(w, d + 8);
    if (!EntOk(w, d) || w[d + 16] != (byte)'c' || w[d + 17] != (byte)'f' || w[d + 18] != (byte)'g' || dsize < 32 || d + dsize > 0x7AFE0 - AREA_LO
        || BitConverter.ToUInt16(w, d + 2) != Crc16(w, d + 32, (int)dsize - 32, 0)) { F.problem = "the 'cfg' directory isn't where it should be"; return F; }
    F.cfgDir = d; F.cfgDirLen = (int)dsize; F.areaLen = d + (int)dsize;
    F.ours = Find(w, (int)F.app.off, (int)F.app.size, "GHR-") >= 0;
    return F;
  }

  // The firmware area (flash 0x4000..0x7AFFF) with our app in place of the factory's, built from YOUR pedal's factory
  // image: every offset, the settings file (cfg_tool.bin), the fixed entries and the 'cfg' directory stay exactly the
  // factory's; only app.bin's data and three checksums change. Checked again by an independent decode.
  public static byte[] BuildArea(byte[] app, byte[] fac, Layout F) {
    if (F.problem != null || F.ours) throw new Exception("internal: not a factory image");
    if (app.Length > F.app.size) throw new Exception("our app doesn't fit the factory's app slot");
    if (app.Length < 4 || app[0] != 0x04 || app[1] != 0x81 || app[2] != 0x80 || app[3] != 0x00) throw new Exception("our app doesn't start the way the bootloader expects");
    var w = new byte[F.areaLen];
    for (int i = 0; i < w.Length; i++) w[i] = 0xFF;
    Array.Copy(app, 0, w, (int)F.app.off, app.Length);
    Array.Copy(F.plain, (int)F.cfg.off, w, (int)F.cfg.off, (int)F.cfg.size);
    Array.Copy(F.plain, 0x60, w, 0x60, 0xA0);
    PutEnt(w, 0x20, Crc16(w, (int)F.app.off, (int)F.app.size, 0), F.app.off, F.app.size, F.app);
    PutEnt(w, 0x40, Crc16(w, (int)F.cfg.off, (int)F.cfg.size, 0), F.cfg.off, F.cfg.size, F.cfg);
    PutEnt(w, 0x00, Crc16(w, 0x20, (int)F.head.size - 0x20, 0), F.head.off, F.head.size, F.head);
    Array.Copy(F.plain, F.cfgDir, w, F.cfgDir, F.cfgDirLen);
    var enc = Sfc(w, 0, w.Length, F.key, 0);
    var region = new byte[AREA_HI - AREA_LO];
    Array.Copy(fac, (int)AREA_LO, region, 0, region.Length);
    Array.Copy(enc, 0, region, 0, enc.Length);
    // independent check: decode what we built as the pedal will
    var img = (byte[])fac.Clone(); Array.Copy(region, 0, img, (int)AREA_LO, region.Length);
    var G = ReadLayout(img, F.key);
    if (G.problem != null) throw new Exception("internal: the built area doesn't decode: " + G.problem);
    if (!G.ours || G.app.off != F.app.off || G.app.size != F.app.size || G.cfg.off != F.cfg.off || G.cfg.size != F.cfg.size || G.head.size != F.head.size || G.areaLen != F.areaLen)
      throw new Exception("internal: the built area's layout differs from the factory's");
    for (int i = 0; i < (int)F.app.size; i++) if (G.plain[F.app.off + i] != (i < app.Length ? app[i] : (byte)0xFF)) throw new Exception("internal: app.bin doesn't read back");
    for (int i = 0; i < F.areaLen; i++) {
      bool appData = i >= F.app.off && i < F.app.off + F.app.size;
      bool crcs = (i >= 0x00 && i < 0x04) || (i >= 0x20 && i < 0x24);
      if (!appData && !crcs && G.plain[i] != F.plain[i]) throw new Exception("internal: the built area differs from the factory's at 0x" + (AREA_LO + i).ToString("X6"));
    }
    for (int i = F.areaLen; i < region.Length; i++) if (region[i] != fac[AREA_LO + i]) throw new Exception("internal: the end of the area changed");
    return region;
  }

  // ------------------------------------------------------------------ the flash, through the loader (or a simulation)
  public abstract class FlashDev {
    public abstract byte[] Read(uint addr, int len);
    public abstract void Erase4K(uint addr);
    public abstract void Write4K(uint addr, byte[] data);
  }
  class LoaderDev : FlashDev {
    int chunk;
    public LoaderDev(int chunk) { this.chunk = chunk; }
    public override byte[] Read(uint addr, int len) {
      var img = new byte[len];
      for (int o = 0; o < len; o += chunk) {
        int n = Math.Min(chunk, len - o);
        for (int tries = 1; ; tries++) {
          try {
            Guard(0xFD05, addr + (uint)o, n);
            var d = new byte[n];
            Exec(Cdb(0xFD05, Cat(BE(addr + (uint)o, 4), BE((uint)n, 2))), d, 1, 10);
            Array.Copy(d, 0, img, o, n); break;
          } catch (Exception e) {
            if (tries >= 8 || e.Message.StartsWith("blocked")) throw;
            L(string.Format("read hiccup at 0x{0:X6} ({1}) - retrying {2}/8", addr + o, e.Message, tries));
            Thread.Sleep(500); string path = devPath; Close(); Open(path);
          }
        }
      }
      return img;
    }
    public override void Erase4K(uint addr) {
      Guard(0xFB01, addr, SECTOR);
      flashTouched = true;
      var r = new byte[16];
      Exec(Cdb(0xFB01, BE(addr, 4)), r, 1, 20);
    }
    public override void Write4K(uint addr, byte[] data) {
      Guard(0xFB04, addr, data.Length);
      GuardData(addr, data);
      flashTouched = true;
      ushort crc = Crc16(data, 0);
      var args = Cat(BE(addr, 4), BE((uint)data.Length, 2), new byte[] { 0, (byte)crc, (byte)(crc >> 8) });
      Exec(Cdb(0xFB04, args), data, 0, 20);
    }
  }

  // ------------------------------------------------------------------ the job
  static string Sha(byte[] b, int off, int n) {
    using (var s = SHA256.Create()) { var h = s.ComputeHash(b, off, n); var sb = new StringBuilder(); foreach (var x in h) sb.Append(x.ToString("x2")); return sb.ToString(); }
  }
  static bool Same(byte[] a, int ao, byte[] b, int bo, int n) { for (int i = 0; i < n; i++) if (a[ao + i] != b[bo + i]) return false; return true; }
  static bool Blank(byte[] a, int o, int n) { for (int i = 0; i < n; i++) if (a[o + i] != 0xFF) return false; return true; }
  static string List(List<uint> d) { var sb = new StringBuilder(); int k = 0; foreach (var a in d) { if (k++ == 12) { sb.Append("..."); break; } sb.Append("0x").Append(a.ToString("X6")).Append(' '); } return sb.ToString().Trim(); }
  static byte[] ReadAll(FlashDev dev, string what) {
    var t0 = DateTime.Now;
    var img = dev.Read(0, SIZE);
    L(string.Format("  read the whole flash ({0}) in {1:F1} s", what, (DateTime.Now - t0).TotalSeconds));
    return img;
  }
  public static byte[] before;
  public static int jobKey = -1;
  public const int OK = 0, NOTHING = 3, HALT = 9;

  // ------------------------------------------------------------------ backups
  // A usable factory backup in this folder: backup_*\flash_full_1MiB.bin, 1 MiB, matching its SHA256SUMS.txt if it
  // has one, and holding a complete factory firmware. The oldest usable one wins (the most likely to be the original).
  public static byte[] FindBackup(string folder, out string where, out int key) {
    where = null; key = -1;
    var dirs = Directory.GetDirectories(folder, "backup_*");
    Array.Sort(dirs, StringComparer.Ordinal);
    foreach (var d in dirs) {
      string f = Path.Combine(d, "flash_full_1MiB.bin");
      if (!File.Exists(f)) continue;
      string why = null;
      byte[] b = File.ReadAllBytes(f);
      if (b.Length != SIZE) why = "not 1 MiB";
      string sums = Path.Combine(d, "SHA256SUMS.txt");
      if (why == null && File.Exists(sums)) {
        string want = null;
        foreach (var line in File.ReadAllLines(sums)) { var t = line.Trim(); if (t.EndsWith("flash_full_1MiB.bin") && t.Length >= 64) want = t.Substring(0, 64).ToLowerInvariant(); }
        if (want != null && want != Sha(b, 0, SIZE)) why = "its fingerprint doesn't match SHA256SUMS.txt (the file changed or is damaged)";
      }
      int k = why == null ? FindKey(b) : -1;
      if (why == null && k < 0) why = "its firmware directory can't be read";
      if (why == null) { var F = ReadLayout(b, k); if (F.problem != null) why = F.problem; else if (F.ours) why = "it holds our firmware, not the factory one"; }
      if (why != null) { L("  (not using " + Path.GetFileName(d) + ": " + why + ")"); continue; }
      where = f; key = k; return b;
    }
    return null;
  }

  // Read the whole flash twice; if both reads match and hold a complete factory firmware, save them as a new backup.
  public static byte[] MakeBackup(FlashDev dev, string folder, int key, bool haveOne, out string where) {
    where = null;
    L("Making a backup of your pedal's flash (reading only - the whole 1 MiB, twice)");
    var a = ReadAll(dev, "backup, first pass");
    var b = ReadAll(dev, "backup, second pass");
    if (!Same(a, 0, b, 0, SIZE)) { L("  The two reads don't match - the USB link isn't reliable. Nothing written. Try another cable or USB port."); return null; }
    int k = FindKey(a);
    if (k != key) { L("  The firmware area can't be read with this pedal's key (" + (k < 0 ? "none found" : "0x" + k.ToString("X4")) + "). Nothing written."); return null; }
    var F = ReadLayout(a, key);
    if (F.problem != null) { L("  The firmware area isn't a complete firmware (" + F.problem + "). Not a good backup - nothing written."); return null; }
    if (F.ours) {
      L("  This pedal already has our firmware, so this read isn't a FACTORY backup. Nothing written.");
      if (!haveOne) L("  Put the backup_... folder from your first install next to this tool. (Without it, RESTORE can't bring the factory firmware back.)");
      return null;
    }
    string dir = Path.Combine(folder, "backup_" + DateTime.Now.ToString("yyyyMMdd_HHmmss"));
    Directory.CreateDirectory(dir);
    string f = Path.Combine(dir, "flash_full_1MiB.bin");
    File.WriteAllBytes(f, a);
    string sha = Sha(a, 0, SIZE);
    File.WriteAllText(Path.Combine(dir, "SHA256SUMS.txt"), sha + " *flash_full_1MiB.bin\r\n");
    File.WriteAllText(Path.Combine(dir, "KEEP THIS FOLDER.txt"),
      "This is your Tank-G's factory flash, read twice and checked on " + DateTime.Now.ToString("yyyy-MM-dd HH:mm") + ".\r\n" +
      "It is the ONLY way back to the factory firmware (RESTORE uses it). Keep a copy somewhere else too.\r\n" +
      "It belongs to your pedal: don't share it (it holds M-VAVE's firmware and your pedal's own settings).\r\n" +
      "sha256 " + sha + "\r\n");
    var back = File.ReadAllBytes(f);
    if (!Same(back, 0, a, 0, SIZE)) { L("  The saved backup doesn't read back right from your disk. Nothing written to the pedal."); return null; }
    where = f;
    L("  backup saved and checked: " + f);
    L("  fingerprint " + sha);
    return a;
  }

  // JieLi's RAM loader for this chip, from kagaimiq's jl-uboot-tool (pinned to one commit, checked by fingerprint)
  const string LOADER_URL = "https://raw.githubusercontent.com/kagaimiq/jl-uboot-tool/adb3f18889e88ac512ce0a3c4d8cc3d3cb30696a/data/loaderblobs/usb/wl82loader.bin";
  static byte[] GetLoader(string folder) {
    string p = Path.Combine(folder, "wl82loader.bin");
    if (File.Exists(p)) {
      var b = File.ReadAllBytes(p);
      if (Sha(b, 0, b.Length) == LOADER_SHA) return b;
      L("wl82loader.bin is here but its fingerprint is wrong - getting a fresh copy");
    }
    L("Getting JieLi's USB loader (wl82loader.bin) from kagaimiq's jl-uboot-tool on GitHub...");
    try {
      ServicePointManager.SecurityProtocol |= (SecurityProtocolType)3072;   // TLS 1.2
      using (var wc = new WebClient()) {
        var b = wc.DownloadData(LOADER_URL);
        if (Sha(b, 0, b.Length) != LOADER_SHA) { L("  what came back has the wrong fingerprint - not using it."); }
        else { File.WriteAllBytes(p, b); L("  got it, fingerprint OK, saved next to this tool."); return b; }
      }
    } catch (Exception e) { L("  couldn't download it (" + e.Message + ")."); }
    L("Download it yourself from:");
    L("  " + LOADER_URL);
    L("save it next to this tool as wl82loader.bin, and run this again. Nothing was sent to the pedal.");
    return null;
  }

  // Make flash [AREA_LO, AREA_HI) equal to want (0x77000 bytes), sector by sector, checking everything.
  public static int Job(FlashDev dev, byte[] backup, byte[] want, string what) {
    if (want.Length != AREA_HI - AREA_LO) throw new Exception("internal: wrong image size");
    target = new byte[SIZE];
    for (int i = 0; i < SIZE; i++) target[i] = 0xFF;
    Array.Copy(want, 0, target, (int)AREA_LO, want.Length);
    L("Checking the pedal's flash first");
    var cur = ReadAll(dev, "before anything");
    before = cur;
    var outside = new List<uint>();
    for (uint a = 0; a < SIZE; a += SECTOR) {
      if (a >= AREA_LO && a < AREA_HI) continue;
      if (a >= VM_LO && a < VM_HI) continue;
      if (!Same(cur, (int)a, backup, (int)a, SECTOR)) outside.Add(a);
    }
    if (outside.Count != 0) {
      L("  The flash differs from your backup OUTSIDE the firmware and settings areas: " + List(outside));
      L("  Not writing anything. (Is this backup from this pedal? " + HELP + ")");
      return NOTHING;
    }
    bool isFactory = Same(cur, (int)AREA_LO, backup, (int)AREA_LO, (int)(AREA_HI - AREA_LO));
    bool isTarget = Same(cur, (int)AREA_LO, target, (int)AREA_LO, (int)(AREA_HI - AREA_LO));
    bool isOurs = oursImage != null && Same(cur, (int)AREA_LO, oursImage, 0, (int)(AREA_HI - AREA_LO));
    var curLay = jobKey >= 0 ? ReadLayout(cur, jobKey) : null;
    string other = curLay == null ? "something else" : curLay.problem != null ? "an unfinished install (" + curLay.problem + ")" : curLay.ours ? "another version of our firmware" : "a different factory firmware than your backup's";
    L("  bootloader, amps/cabs and the rest: exactly your backup. The firmware area holds: " + (isTarget ? "already " + what : isFactory ? "the factory firmware" : isOurs ? "our firmware (" + OURS_NAME + " " + OURS_VER + ")" : other));
    L("  (this read is saved as a fresh full backup next to the report)");
    var todo = new List<uint>();
    for (uint a = AREA_LO; a < AREA_HI; a += SECTOR) if (!Same(cur, (int)a, target, (int)a, SECTOR)) todo.Add(a);
    if (todo.Count == 0) { L("  Nothing to do: the pedal already has " + what + "."); return OK; }
    L("");
    L("Writing " + what + ": " + todo.Count + " of " + (AREA_HI - AREA_LO) / SECTOR + " sectors (4 KB each) between 0x" + AREA_LO.ToString("X6") + " and 0x" + AREA_HI.ToString("X6"));
    var t0 = DateTime.Now;
    int n = 0;
    // The directory (first sector) goes first and comes back last: erased before anything else, written after
    // everything else. The pedal's bootloader switches itself into its USB loader when it finds no valid
    // directory, so if anything stops half way, the pedal comes up in its loader and this can simply run again.
    if (todo.Contains(AREA_LO)) {
      bool gone = false;
      for (int e = 1; e <= 3 && !gone; e++) { dev.Erase4K(AREA_LO); gone = Blank(dev.Read(AREA_LO, SECTOR), 0, SECTOR); }
      if (!gone) {
        var now0 = ReadAll(dev, "to see where things stand");
        if (Same(now0, 0, cur, 0, SIZE)) { L("  The first erase didn't happen at all. Stopping, all unchanged."); return NOTHING; }
        L("  !! the directory sector wouldn't erase. " + STUCK);
        return HALT;
      }
      L("  erased the firmware directory first (from here until it's written back last, the pedal would start in its loader)");
      todo.Remove(AREA_LO);
      todo.Add(AREA_LO);
    }
    foreach (var a in todo) {
      var want4 = new byte[SECTOR]; Array.Copy(target, (int)a, want4, 0, SECTOR);
      bool ok = false;
      for (int attempt = 1; attempt <= 3 && !ok; attempt++) {
        dev.Erase4K(a);
        var r = dev.Read(a, SECTOR);
        if (!Blank(r, 0, SECTOR)) { L("  0x" + a.ToString("X6") + ": not blank after erase " + attempt); continue; }
        if (Blank(want4, 0, SECTOR)) { ok = true; break; }
        for (int w = 1; w <= 2 && !ok; w++) {
          dev.Write4K(a, want4);
          r = dev.Read(a, SECTOR);
          if (Same(r, 0, want4, 0, SECTOR)) ok = true;
          else L("  0x" + a.ToString("X6") + ": write " + w + " didn't read back right");
        }
      }
      if (!ok) {
        L("  !! sector 0x" + a.ToString("X6") + " wouldn't take.");
        var now = ReadAll(dev, "to see where things stand");
        if (Same(now, 0, cur, 0, SIZE)) { L("  Nothing in the flash has changed (the erase/write didn't happen at all). Stopping, all unchanged."); return NOTHING; }
        L("  !! " + STUCK);
        return HALT;
      }
      if (++n % 16 == 0) Console.Write("\r  " + n + " / " + todo.Count + " sectors   ");
    }
    Console.WriteLine();
    L(string.Format("  wrote and checked {0} sectors in {1:F1} s", todo.Count, (DateTime.Now - t0).TotalSeconds));
    var after = ReadAll(dev, "after writing");
    var bad = new List<uint>();
    for (uint a = 0; a < SIZE; a += SECTOR) {
      bool inArea = a >= AREA_LO && a < AREA_HI;
      if (!Same(after, (int)a, inArea ? target : cur, (int)a, SECTOR)) bad.Add(a);
    }
    if (bad.Count != 0) { L("  !! these sectors aren't right: " + List(bad) + ". " + STUCK); return HALT; }
    L("  the firmware area is exactly " + what + ", and every other byte of the flash is exactly as it was");
    return OK;
  }

  // ------------------------------------------------------------------ main
  static bool IsFactoryId(string id) { return id != null && id.StartsWith("TANK-G") && !id.StartsWith("GHR-"); }   // e.g. TANK-Gv2_097
  static readonly string LOADER_SHA = "d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376";
  public static readonly string OURS_SHA = "cf8cef6c78438eea8b2d33608a51464397ee104e02500df74ddbebbd53350d16";
  const string OURS_FILE = "ghr_ambient_app.bin";
  const string HELP = "post the report.txt from this run in an Issue on the project's GitHub page.";
  const string STUCK = "Don't panic: the pedal can always be fixed from its loader. LEAVE IT PLUGGED IN and run 3_RESTORE_FACTORY.bat\r\n     (or the install again) right away. If that stops too, " + HELP;
  const string OURS_VER = "0.11";
  const string OURS_NAME = "GHR AMBIENT";
  public static byte[] oursImage;   // the current version of ours, to recognise it on the pedal

  public static int Run(string folder, string mode) {
    string m = mode.ToLowerInvariant();
    string outDir = Path.Combine(folder, (m == "backup" ? "backup-run" : m) + "_" + DateTime.Now.ToString("yyyyMMdd_HHmmss"));   // never backup_*: that's a backup
    Directory.CreateDirectory(outDir);
    logPath = Path.Combine(outDir, "report.txt");
    int rc = 1;
    try { rc = Main2(folder, outDir, mode.ToUpperInvariant()); }
    catch (Exception e) {
      L("STOPPED: " + e.Message);
      if (flashTouched) L("!! This happened after writing had started. " + STUCK);
      else L("Nothing was written to the flash. Unplug the pedal, wait 5 s, plug it back in, then try again.");
    }
    finally {
      if (before != null) {
        try {
          File.WriteAllBytes(Path.Combine(outDir, "flash_before_1MiB.bin"), before);
          File.WriteAllText(Path.Combine(outDir, "SHA256SUMS.txt"), Sha(before, 0, SIZE) + " *flash_before_1MiB.bin\r\n");
        } catch (Exception) { }
      }
      Close();
      logPath = null;
      try { File.WriteAllText(Path.Combine(outDir, "report.txt"), log.ToString()); } catch (Exception) { }
    }
    return rc;
  }

  // a UBOOT disk that is already there, or one that appears within `seconds`
  static string[] FindUboot(int seconds, uint ignore) {
    var until = DateTime.Now.AddSeconds(seconds);
    do {
      uint now = GetLogicalDrives();
      for (int i = 2; i < 26; i++) {
        if ((now & (1u << i)) == 0 || (ignore & (1u << i)) != 0) continue;
        string root = (char)('A' + i) + ":\\";
        if (GetDriveTypeW(root) != 2) continue;            // removable disks only
        string p = @"\\.\" + (char)('A' + i) + ":";
        try { Open(p); var q = Inquiry(); if (q[1].StartsWith("UBOOT")) return q; Close(); } catch { Close(); }
      }
      Thread.Sleep(500);
    } while (DateTime.Now < until);
    return null;
  }

  // our firmware's status block: read it through the ROM loader, save it, print it
  static void ReadStatus(string outDir, string name, string path) {
    try {
      var st = new byte[1024];
      Array.Copy(MengLi(MemRead(STAT_ADDR, 512)), 0, st, 0, 512);
      Array.Copy(MengLi(MemRead(STAT_ADDR + 512, 512)), 0, st, 512, 512);
      File.WriteAllBytes(Path.Combine(outDir, name), st);
      PrintStatus(st);
    } catch (Exception e) {
      L("(couldn't read the status block: " + e.Message + ")");
      Close(); Thread.Sleep(1500); Open(path);
    }
  }

  static void PrintStatus(byte[] st) {
    Func<int, uint> W = i => BitConverter.ToUInt32(st, i * 4);
    if (W(0) != 0x7A6B0001) return;
    string[] why = { "?", "footswitch A held at power-up", "the watchdog had reset it (it hung)", "the audio clock stopped", "A + D held", "B + C hang test", "the 'go to loader' message over USB" };
    const double HU = 64 * 1e6 / 44100.0;
    L("our firmware's last report: version " + W(2) + ", went to the loader because: " + (W(1) < 7 ? why[W(1)] : W(1).ToString()) + ", reset source 0x" + W(3).ToString("X2"));
    if (W(1) < 2) return;     // it went straight to the loader at power-up: the rest is whatever was in RAM
    if (W(6) != 0) {
      double i0 = W(15), avg = W(18), mn = W(16);
      L(string.Format("  ran {0:0} s; CPU busy {1:0}% average, {2:0}% worst; overruns {3}; delay loop {4}; SPI BAUD {5}; ADC ok {6} / timeouts {7}; ring frames {8}",
        W(6) * HU / 1e6, i0 > 0 ? 100 * (1 - avg / i0) : -1, i0 > 0 ? 100 * (1 - mn / i0) : -1, W(17), W(14), W(10), W(19), W(20), W(26)));
    }
    if (W(2) >= 2) L(string.Format("  USB: {0}, configured {1}; setups {2}, identity answers {3}, packets in {4} / out {5}{8}, bus resets {6}, restarts {7}",
      (W(38) & 1) != 0 ? "up" : "DOWN", (W(38) >> 8) & 0xFF, W(39), W(40), W(41), W(42), W(43), W(44), W(2) >= 4 ? " (taken by the PC " + W(45) + ")" : ""));
    if (W(2) >= 6) {
      string[] cs = { "recording (no cloud)", "cloud fading in", "cloud held", "cloud fading out" };
      L(string.Format("  ambient: {0}{1}; grabs {2}, live clouds {3}, grains {4} (now {5}, at most {6}), rot passes {7} / events {8}, cloud level x{9:0.0}, memory {10:0.0} s, CPU guard steps {11}",
        (W(46) & 0xF) < 4 ? cs[W(46) & 0xF] : "?", ((W(46) >> 4) & 1) != 0 ? " (LIVE)" : "", W(47), W(2) >= 7 ? W(54) : 0, W(48), (W(46) >> 8) & 0xFF, (W(46) >> 16) & 0xFF, W(49), W(50), W(51) / 4096.0, W(52) / 22050.0, W(53)));
      if (W(2) >= 9) L(string.Format("  switches: SPACE {0}, SMEAR {1}, WARBLE {2}, dry guitar {3}", (W(32) & 2) != 0 ? "on" : "off", (W(32) & 4) != 0 ? "on" : "off", (W(32) & 8) != 0 ? "on" : "off", (W(32) & 0x100) != 0 ? "OFF (wet only)" : "on"));
      if (W(2) >= 11) L(string.Format("  levels: guitar in, loudest {0:0.0} dBFS; out (after MASTER, before the soft limiter) loudest {1:0.0} dBFS; limiter squashing hard {2} times{3}",
        W(29) > 0 ? 20.0 * Math.Log10(W(29) / 32767.0) : -99.0, W(30) > 0 ? 20.0 * Math.Log10(W(30) / 32767.0) : -99.0, W(31),
        W(31) > 100 ? " - turn MASTER (or CLOUD / RVB MIX / DLY MIX) down a little" : ""));
      if (W(2) >= 7) L(string.Format("  compressor: deepest {0:0.0} dB " + (W(2) >= 11 ? "this session" : "in the last second") + "; SMEAR swells {1}{2}",
        W(55) > 0 && W(55) <= 32768 ? 20.0 * Math.Log10(W(55) / 32768.0) : 0.0, W(56), W(2) >= 10 ? "; bender clones / repeats " + W(57) : ""));
    }
    if (W(2) >= 3 && W(152) == 0x5A5A0080) {
      string[] rn = { "PLL 16A00", "PLL 16A04", "PLL 16A08", "PLL 16A0C", "PWR 10000", "10004", "10008", "1000C", "CLK1 10010", "CLK2 10014", "119A0", "40404", "4040C",
                      "5101C", "USBIO 51000", "USB0 11800", "USB1 11804", "40400", "40408", "10018", "1001C", "16A10", "16A14", "13E04" };
      if (W(2) >= 4) { rn[17] = "CACHE 1EEE008"; rn[18] = "DWAY 1EEE00C"; }
      var sb1 = new StringBuilder(); var sb2 = new StringBuilder();
      for (int k = 0; k < 24; k++) sb1.Append(rn[k]).Append('=').Append(W(128 + k).ToString("X8")).Append(k % 6 == 5 ? "\r\n    " : "  ");
      L("  registers at start:\r\n    " + sb1.ToString());
      if (W(184) == 0x5A5A00A0) {
        for (int k = 0; k < 24; k++) if (W(160 + k) != W(128 + k)) sb2.Append(rn[k]).Append('=').Append(W(160 + k).ToString("X8")).Append("  ");
        L("  changed by USB start: " + sb2.ToString() + "| usb up " + W(190) + ", SIE timeouts " + W(191));
      }
    }
  }

  static string WaitOurs(int seconds) {
    var until = DateTime.Now.AddSeconds(seconds);
    while (DateTime.Now < until) { Thread.Sleep(1500); string s2 = Identify(); if (s2 != null && s2.StartsWith("GHR-")) return s2; }
    return null;
  }

  // STATUS: read our firmware's report (no loader upload, nothing written). Our firmware with USB takes the
  // 'go to loader' message; without USB, A + D sends it to the loader.
  static int StatusOnly(string outDir) {
    L("TANK-G: READ OUR FIRMWARE'S REPORT (nothing is written). Report goes to " + outDir);
    string sid = Identify();
    uint drivesBefore = GetLogicalDrives();
    string[] q = null;
    if (!string.IsNullOrEmpty(sid) && sid.StartsWith("GHR-")) {
      L("pedal says: '" + sid + "' - our firmware is running AND answering over USB. Sending it to its loader to read the report.");
      SysEx(new byte[] { 0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7 }, 300);
      q = FindUboot(40, drivesBefore);
    } else if (!string.IsNullOrEmpty(sid)) {
      L("pedal says: '" + sid + "' - that's not our firmware, nothing to read."); return 1;
    } else {
      L("The pedal isn't answering over USB.");
      q = FindUboot(1, 0);
      if (q == null) {
        Console.WriteLine();
        Console.WriteLine("  >>> HOLD A + D for 2 s (three beeps). Waiting 2 minutes...");
        q = FindUboot(120, 0);
      }
    }
    if (q == null || q[1] != "UBOOT1.00") { L("The loader disk didn't show up - nothing read. Unplug the pedal, wait 5 s, plug it in and try again."); return 1; }
    L("loader disk " + devPath + "  '" + q[0] + "' '" + q[1] + "'");
    Thread.Sleep(1500);
    ReadStatus(outDir, "firmware_status.bin", devPath);
    Close();
    L("Done. Unplug the pedal, wait 5 s, plug it back in to play.");
    return OK;
  }

  static int Main2(string folder, string outDir, string mode) {
    bool install = mode == "INSTALL", backupOnly = mode == "BACKUP";
    if (mode == "STATUS") return StatusOnly(outDir);
    if (!install && !backupOnly && mode != "RESTORE") { L("unknown mode " + mode); return 1; }
    L("TANK-G " + (install ? "INSTALL OUR FIRMWARE (" + OURS_NAME + " " + OURS_VER + ")" : backupOnly ? "BACKUP (reading only)" : "RESTORE THE FACTORY FIRMWARE") + ". Report goes to " + outDir);
    byte[] app = null;
    try { app = File.ReadAllBytes(Path.Combine(folder, OURS_FILE)); } catch (Exception) { }
    if (app != null && Sha(app, 0, app.Length) != OURS_SHA) app = null;   // (restore also uses it, to recognise our firmware on the pedal)
    if (install) {
      if (app == null) { L(OURS_FILE + " is missing or its fingerprint is wrong - stopping, nothing sent."); return 1; }
      L("our firmware: " + OURS_FILE + " (fingerprint OK)");
    }
    string bkPath; int bkKey;
    byte[] bk = FindBackup(folder, out bkPath, out bkKey);
    if (bk != null) L("your factory backup: " + bkPath + " (checked)");
    else if (mode == "RESTORE") { L("There's no factory backup (backup_...\\flash_full_1MiB.bin) next to this tool, so there's nothing to restore from. Nothing sent."); return 1; }
    else L("No factory backup here yet: this run makes one first (reading only), before anything is written.");
    byte[] loader = GetLoader(folder);
    if (loader == null) return 1;
    loaderLen = loader.Length;

    // get the pedal into its loader
    string id = Identify();
    string[] inq = null;
    uint drivesBefore = GetLogicalDrives();
    if (IsFactoryId(id)) {
      L("pedal says: '" + id + "' (the factory firmware is running)");
    } else if (id != null && id.StartsWith("GHR-")) {
      L("pedal says: '" + id + "' (OUR firmware is running - it takes the same 'go to loader' message)");
    } else {
      L(id == null ? "The pedal isn't answering over USB." : "pedal says: '" + id + "'");
      inq = FindUboot(1, 0);
      if (inq == null) {
        Console.WriteLine();
        Console.WriteLine("  >>> UNPLUG the pedal. HOLD footswitch A, plug it back in, keep holding 2 s. Waiting 2 minutes...");
        inq = FindUboot(120, 0);
        if (inq == null) { L("The loader disk didn't show up. Nothing was written. Plug the pedal in normally and try again."); return 1; }
      }
      L("found the loader already waiting: " + devPath);
    }
    Console.WriteLine();
    if (backupOnly) Console.WriteLine("Ready to make a BACKUP: the pedal goes into its loader, the whole flash is read twice, nothing is written.");
    else Console.WriteLine(install
      ? (bk == null ? "Ready to BACK UP your pedal and then INSTALL our firmware.\r\n" : "Ready to INSTALL our firmware.\r\n") +
        "It replaces the factory firmware (only the firmware area: your settings, amps/cabs and the bootloader stay\r\nexactly as they are). RESTORE puts the factory one back any time."
      : "Ready to RESTORE the factory firmware from your backup (only the firmware area is written).");
    Console.WriteLine("If Windows offers to FORMAT a disk, click CANCEL. Don't unplug or switch off the pedal until it says DONE.");
    Console.Write("Type " + mode + " and press Enter to go (anything else stops, nothing written): ");
    string ans = Console.ReadLine();
    if (ans == null || ans.Trim().ToUpperInvariant() != mode) { L("cancelled by you - nothing written."); return 1; }

    if (inq == null) {
      SysEx(new byte[] { 0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7 }, 300);
      L("sent loader message F0 22 24 35 7D F7");
      inq = FindUboot(40, drivesBefore);
      if (inq == null) { L("The loader disk didn't appear within 40 s. Power-cycle the pedal; nothing was written."); return 1; }
    }
    L("loader disk " + devPath + "  '" + inq[0] + "' '" + inq[1] + "'");
    if (inq[1] != "UBOOT1.00") { L("unexpected loader version - stopping"); return 1; }
    Thread.Sleep(1500);
    string ldPath = devPath;

    // our firmware's status block (it leaves one in RAM before it goes to the loader)
    ReadStatus(outDir, "firmware_status.bin", ldPath);

    for (int attempt = 1; ; attempt++) {
      try {
        for (int o = 0; o < loader.Length; o += 512) {
          int n = Math.Min(512, loader.Length - o); var blk = new byte[n]; Array.Copy(loader, o, blk, 0, n);
          MemWrite(LOADER_ADDR + (uint)o, blk);
        }
        break;
      } catch (Exception e) {
        if (attempt >= 4) throw;
        L("  copying the loader into RAM hiccuped (" + e.Message + ") - trying again " + attempt + "/3");
        Close(); Thread.Sleep(2000); Open(ldPath);
      }
    }
    Cmd(0xFB08, Cat(BE(LOADER_ADDR, 4), BE(1, 2)), true);
    L("loader copied to RAM and started");
    Thread.Sleep(300);
    var od = Cmd(0xFC0A, new byte[0], true);
    uint flashId = (uint)(od[2] | (od[3] << 8) | (od[4] << 16) | (od[5] << 24));
    int key = ChipKey();
    var bs = Cmd(0xFC14, new byte[0], true);
    uint buf = (uint)((bs[0] << 24) | (bs[1] << 16) | (bs[2] << 8) | bs[3]);
    L(string.Format("flash type 0x{0:X2} id 0x{1:X6}, chip key 0x{2:X4}, loader buffer {3}", od[0], flashId, key, buf));
    // a 1 MiB SPI flash (the last byte of the ID is log2 of the size); 0x856014 on the pedals seen so far
    if (od[0] != 3 || (flashId & 0xFF) != 0x14) { L("this isn't the 1 MiB flash a Tank-G has - stopping, nothing written."); return 1; }
    int chunk = (buf >= 4096 && buf <= 32768) ? (int)buf : 512;
    var dev = new LoaderDev(chunk);
    jobKey = key;

    if (bk == null) {
      bk = MakeBackup(dev, folder, key, false, out bkPath);
      if (bk == null) { try { Cmd(0xFC0C, BE(1, 4), false); } catch (Exception) { } Close(); L("DONE: nothing was written. Unplug the pedal, wait 5 s, plug it back in."); return NOTHING; }
      L("  >>> Copy that backup_... folder somewhere safe too (a USB stick, the cloud). It's your way back.");
    } else if (bkKey != key) {
      L("Your backup was made on a pedal with a different chip key (0x" + bkKey.ToString("X4") + "): it isn't this pedal's. Nothing written.");
      return 1;
    } else if (backupOnly) {
      var nb = MakeBackup(dev, folder, key, true, out bkPath);
      if (nb == null) L("(no new backup made - your existing one is still there and fine)");
    }
    if (backupOnly) {
      try { Cmd(0xFC0C, BE(1, 4), false); } catch (Exception) { }
      Close();
      L("DONE: nothing was written. Unplug the pedal, wait 5 s, plug it back in.");
      return OK;
    }

    var F = ReadLayout(bk, key);
    byte[] want; string what;
    oursImage = app != null ? BuildArea(app, bk, F) : null;
    if (install) {
      want = oursImage; what = "our firmware (" + OURS_NAME + " " + OURS_VER + ")";
      L("built our firmware area from " + OURS_FILE + " and your backup (checked by decoding it again)");
    } else {
      want = new byte[AREA_HI - AREA_LO]; Array.Copy(bk, (int)AREA_LO, want, 0, want.Length); what = "the factory firmware from your backup";
    }

    int rc = Job(dev, bk, want, what);
    if (rc == HALT) return rc;
    try { Cmd(0xFC0C, BE(1, 4), false); } catch (Exception) { }
    Close();
    Console.WriteLine();
    if (rc != OK) { L("DONE: nothing was changed. Unplug the pedal, wait 5 s, plug it back in."); return rc; }
    if (install) {
      L("DONE: our firmware is on the pedal and checked byte for byte.");
      Console.WriteLine();
      Console.WriteLine("  >>> UNPLUG the pedal, wait 5 s, plug it back in (don't hold anything). Then play!");
      uint ldBit = (ldPath.Length >= 6) ? 1u << (char.ToUpperInvariant(ldPath[4]) - 'A') : 0;
      var untilG = DateTime.Now.AddSeconds(180);
      while (DateTime.Now < untilG && (GetLogicalDrives() & ldBit) != 0) Thread.Sleep(500);
      if ((GetLogicalDrives() & ldBit) != 0) { L("(The pedal is still in its loader. Unplug it, wait 5 s, plug it back in - it will play.)"); L("Factory firmware back any time: 3_RESTORE_FACTORY.bat."); return OK; }
      L("unplugged - checking that it starts (up to 60 s)...");
      string ourId = WaitOurs(60);
      if (ourId != null) L("pedal now: " + ourId + " - GHR AMBIENT is running. Play!");
      else L("It didn't answer over USB yet. If its lights come on and it plays, all is well. If not, run 3_RESTORE_FACTORY.bat, or " + HELP);
      L("Factory firmware back any time: 3_RESTORE_FACTORY.bat.");
      return OK;
    }
    L("DONE: the factory firmware is back, checked byte for byte.");
    L("Now UNPLUG the pedal, wait 5 s and plug it back in. Waiting up to 90 s for it to answer as the factory firmware...");
    var until = DateTime.Now.AddSeconds(90);
    string back = null;
    while (DateTime.Now < until) { Thread.Sleep(1500); string s = Identify(); if (IsFactoryId(s)) { back = s; break; } }
    L("pedal now: " + (back ?? "not answering yet (if it lights up and plays normally, that's fine)"));
    return back != null ? OK : 2;
  }
}
