// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 GerbilHamster Records
// Simulated pedal for the new installer: format code, backups, and the write job.
using System; using System.IO; using System.Reflection; using System.Collections.Generic;
public class FDev2 : TankFlash.FlashDev {
  public byte[] f; public string mode; public int writes = 0, erases = 0, reads = 0;
  public FDev2(byte[] img, string mode) { f = (byte[])img.Clone(); this.mode = mode; }
  public override byte[] Read(uint a, int n) {
    var d = new byte[n]; Array.Copy(f, (int)a, d, 0, n);
    if (mode == "flaky_read" && n == 0x100000 && (reads++ % 2) == 1) d[0x55555] ^= 0x10;
    return d;
  }
  public override void Erase4K(uint a) {
    typeof(TankFlash).GetMethod("Guard", BindingFlags.NonPublic | BindingFlags.Static).Invoke(null, new object[] { 0xFB01, a, 4096 });
    erases++; for (int i = 0; i < 4096; i++) f[a + i] = 0xFF;
  }
  public override void Write4K(uint a, byte[] d) {
    var g = typeof(TankFlash);
    g.GetMethod("Guard", BindingFlags.NonPublic | BindingFlags.Static).Invoke(null, new object[] { 0xFB04, a, d.Length });
    g.GetMethod("GuardData", BindingFlags.NonPublic | BindingFlags.Static).Invoke(null, new object[] { a, d });
    writes++; for (int i = 0; i < 4096; i++) f[a + i] &= d[i];
  }
}
public static class FSim2 {
  static int fails = 0;
  static void Check(bool ok, string what) { Console.WriteLine((ok ? "  ok    " : "  FAIL  ") + what); if (!ok) fails++; }
  static bool Eq(byte[] a, int ao, byte[] b, int bo, int n) { for (int i = 0; i < n; i++) if (a[ao + i] != b[bo + i]) return false; return true; }
  public static int Main(string[] args) {
    byte[] bk = File.ReadAllBytes(args[0]);          // factory backup
    byte[] app = File.ReadAllBytes(args[1]);         // our app.bin
    // args: your flash_full_1MiB.bin, ghr_ambient_app.bin, a reference area image to compare with (or -), a scratch folder
    bool haveRef = args[2] != "-";
    byte[] pyArea = haveRef ? File.ReadAllBytes(args[2]) : null;   // e.g. an area built by another tool
    string tmp = args[3];

    Console.WriteLine("== format");
    int key = TankFlash.FindKey(bk);
    Check(key >= 0, "FindKey finds the key, and only one (0x" + key.ToString("X4") + ")");
    var F = TankFlash.ReadLayout(bk, key);
    Check(F.problem == null && !F.ours, "factory backup decodes as a complete factory firmware (" + (F.problem ?? "ok") + ")");
    Console.WriteLine(string.Format("  layout: app 0x{0:X} bytes at 0x120, cfg_tool.bin at 0x{1:X}, cfg dir at 0x{2:X}, area ends 0x{3:X} (TANK-Gv2_097: 0x75400, 0x75520, 0x7569F, 0x7A218)", F.app.size, F.cfg.off, F.cfgDir, 0x4000 + F.areaLen));
    var area = TankFlash.BuildArea(app, bk, F);
    if (haveRef) Check(area.Length == pyArea.Length && Eq(area, 0, pyArea, 0, area.Length), "built area is byte-identical to the reference image");
    var inst = (byte[])bk.Clone(); Array.Copy(area, 0, inst, 0x4000, area.Length);
    var G = TankFlash.ReadLayout(inst, key);
    Check(G.problem == null && G.ours, "installed image decodes as OURS");
    Check(TankFlash.ReadLayout(bk, key ^ 1).problem != null, "a wrong key doesn't decode");
    var half = (byte[])inst.Clone(); for (int i = 0x20000; i < 0x21000; i++) half[i] = 0xFF;
    Check(TankFlash.ReadLayout(half, key).problem != null, "a half-written area is caught (" + TankFlash.ReadLayout(half, key).problem + ")");
    bool threw = false; try { TankFlash.BuildArea(new byte[0x76000], bk, F); } catch (Exception) { threw = true; }
    Check(threw, "an app too big for the slot is refused");
    threw = false; var badApp = (byte[])app.Clone(); badApp[0] = 0; try { TankFlash.BuildArea(badApp, bk, F); } catch (Exception) { threw = true; }
    Check(threw, "an app without the 'goto +4; rts' entry is refused");
    threw = false; try { TankFlash.BuildArea(app, inst, G); } catch (Exception) { threw = true; }
    Check(threw, "building from an image that already has ours is refused");
    // a pedal with a different (made-up) key: re-encipher the factory area with key 0x1234 and check it all still works
    var sfc = typeof(TankFlash).GetMethod("Sfc", BindingFlags.NonPublic | BindingFlags.Static);
    var plain = (byte[])sfc.Invoke(null, new object[] { bk, 0x4000, 0x77000, key, 0 });
    var other = (byte[])bk.Clone();
    int k1 = key == 0x1234 ? 0x4321 : 0x1234;
    var enc2 = (byte[])sfc.Invoke(null, new object[] { plain, 0, F.areaLen, k1, 0 });
    Array.Copy(enc2, 0, other, 0x4000, enc2.Length);
    int k2 = TankFlash.FindKey(other);
    var F2 = TankFlash.ReadLayout(other, k2);
    Check(k2 == k1 && F2.problem == null, "another pedal's key is found from its own backup");
    var area2 = TankFlash.BuildArea(app, other, F2);
    var inst2 = (byte[])other.Clone(); Array.Copy(area2, 0, inst2, 0x4000, area2.Length);
    var G2 = TankFlash.ReadLayout(inst2, k2);
    Check(G2.problem == null && G2.ours && Eq(G2.plain, 0x120, app, 0, app.Length), "and our firmware builds for it");

    Console.WriteLine("== backups");
    if (Directory.Exists(tmp)) Directory.Delete(tmp, true);
    Directory.CreateDirectory(tmp);
    string where; int bkKey;
    Check(TankFlash.FindBackup(tmp, out where, out bkKey) == null, "empty folder: no backup");
    string w2;
    var made = TankFlash.MakeBackup(new FDev2(bk, "ok"), tmp, key, false, out w2);
    Check(made != null && Eq(made, 0, bk, 0, bk.Length) && File.Exists(w2), "MakeBackup on a factory pedal saves it");
    var found = TankFlash.FindBackup(tmp, out where, out bkKey);
    Check(found != null && where == w2 && bkKey == key, "FindBackup finds it again (with its SHA256SUMS)");
    Check(TankFlash.MakeBackup(new FDev2(inst, "ok"), tmp + "_x", key, false, out w2) == null, "MakeBackup refuses a pedal that already has ours");
    Directory.CreateDirectory(tmp + "_y");
    Check(TankFlash.MakeBackup(new FDev2(bk, "flaky_read"), tmp + "_y", key, false, out w2) == null, "MakeBackup refuses when the two reads differ");
    Check(TankFlash.MakeBackup(new FDev2(bk, "ok"), tmp + "_y", key ^ 0x1111, false, out w2) == null, "MakeBackup refuses when the chip key doesn't fit");
    // tampered backup is skipped; an older good one still wins; a backup holding ours is skipped
    string t2 = tmp + "_z"; if (Directory.Exists(t2)) Directory.Delete(t2, true);
    Directory.CreateDirectory(Path.Combine(t2, "backup_20200101_000000"));
    var bad = (byte[])bk.Clone(); bad[0x90000] ^= 1;
    File.WriteAllBytes(Path.Combine(t2, "backup_20200101_000000", "flash_full_1MiB.bin"), bad);
    File.Copy(Path.Combine(Path.GetDirectoryName(where), "SHA256SUMS.txt"), Path.Combine(t2, "backup_20200101_000000", "SHA256SUMS.txt"));   // the good file's fingerprint
    Directory.CreateDirectory(Path.Combine(t2, "backup_20210101_000000"));
    File.WriteAllBytes(Path.Combine(t2, "backup_20210101_000000", "flash_full_1MiB.bin"), inst);
    Directory.CreateDirectory(Path.Combine(t2, "backup_20220101_000000"));
    File.WriteAllBytes(Path.Combine(t2, "backup_20220101_000000", "flash_full_1MiB.bin"), bk);   // no SHA file: still OK
    found = TankFlash.FindBackup(t2, out where, out bkKey);
    Check(found != null && where.Contains("backup_20220101_000000"), "tampered and 'ours' backups skipped, the good one used (" + where + ")");

    Console.WriteLine("== write job (install / restore)");
    TankFlash.jobKey = key;
    TankFlash.oursImage = area;
    var fac = new byte[0x77000]; Array.Copy(bk, 0x4000, fac, 0, fac.Length);
    var cases = new object[][] {
      new object[] { "install on a factory pedal", bk, area, 0 },
      new object[] { "restore from ours", inst, fac, 0 },
      new object[] { "install again (already there)", inst, area, 0 },
      new object[] { "install over a half install", half, area, 0 },
      new object[] { "install, bootloader differs from backup", Flip(bk, 0x1000), area, 3 },
      new object[] { "install, settings changed (allowed)", Flip(bk, 0x7C123), area, 0 },
    };
    foreach (var c in cases) {
      var dev = new FDev2((byte[])c[1], "ok");
      int rc = TankFlash.Job(dev, bk, (byte[])c[2], "X");
      byte[] start = (byte[])c[1], want = (byte[])c[2];
      int areaBad = 0, otherBad = 0;
      for (int i = 0; i < bk.Length; i++) { bool inA = i >= 0x4000 && i < 0x7B000; if (inA ? dev.f[i] != (rc == 0 ? want[i - 0x4000] : start[i]) : dev.f[i] != start[i]) { if (inA) areaBad++; else otherBad++; } }
      Check(rc == (int)c[3] && areaBad == 0 && otherBad == 0, c[0] + ": rc " + rc + ", area bytes wrong " + areaBad + ", other bytes changed " + otherBad + ", writes " + dev.writes);
    }
    Console.WriteLine(fails == 0 ? "ALL PASSED" : fails + " FAILED");
    return fails;
  }
  static byte[] Flip(byte[] b, int at) { var x = (byte[])b.Clone(); x[at] ^= 1; return x; }
}
