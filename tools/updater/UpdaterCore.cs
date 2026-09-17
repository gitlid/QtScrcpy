// QtScrcpy offline, script-preserving updater. .NET Framework 4.5+.
// No network requests, process termination, registry writes or AppData writes.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Xml;
using System.Xml.Linq;

namespace QscUpdate {
public sealed class UpdateResult {
    public string BackupDirectory;
    public int Replaced, Added, Preserved;
}
internal sealed class Item {
    public string Name, Hash;
    public long Size;
}
internal sealed class Change {
    public string Name, OldHash, NewHash;
    public bool Existed;
}
public static class Updater {
    public const string Version = "0.4.4-rc.1";
    public const string PayloadHash = "1e966081f8f5ce03a1ca518c5b7c008b1cc7da7d1de4ffbcfd4ac4a612213e41";
    const string Prefix = "QtScrcpy-keymapper-0.4.4-rc.1-win64/";
    const string Format = "QtScrcpy-safe-update-v1";
    static readonly StringComparer Names = StringComparer.OrdinalIgnoreCase;
#if UPDATER_TEST
    internal static Action<string,int> Fault;
    internal sealed class SimulatedCrash : Exception { }
#endif
    static void Checkpoint(string phase, int index) {
#if UPDATER_TEST
        if (Fault != null) Fault(phase, index);
#endif
    }
    public static string Hash(Stream stream) {
        using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
    }
    public static string HashFile(string path) {
        using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read)) return Hash(stream);
    }
    static string Canonical(string path) { return Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar); }
    public static bool Inside(string path, string root) {
        return Canonical(path).StartsWith(Canonical(root) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }
    public static string BackupRoot(string target) {
        target = Canonical(target);
        return Path.Combine(Path.GetDirectoryName(target), Path.GetFileName(target) + ".qsc-update-backups");
    }
    // Windows path rules are applied even in non-Windows unit tests.
    internal static void ValidateRelative(string path) {
        if (String.IsNullOrEmpty(path) || path.Length > 200 || path.Contains('\\') || path.IndexOfAny(new[]{':','\0','\r','\n','\t','*','?','"','<','>','|'}) >= 0 || path.StartsWith("/"))
            throw new InvalidDataException("不安全的文件路径：" + path);
        foreach (string part in path.Split('/')) {
            if (part.Length == 0 || part == "." || part == ".." || part.EndsWith(".") || part.EndsWith(" "))
                throw new InvalidDataException("不安全的路径片段：" + path);
            string stem = part.Split('.')[0].ToUpperInvariant();
            if (new[]{"CON","PRN","AUX","NUL","CLOCK$"}.Contains(stem) ||
                (stem.Length == 4 && (stem.StartsWith("COM") || stem.StartsWith("LPT")) && stem[3] >= '0' && stem[3] <= '9'))
                throw new InvalidDataException("不允许的 Windows 设备名：" + path);
        }
    }
    static string At(string root, string relative) {
        ValidateRelative(relative);
        string full = Path.GetFullPath(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));
        if (!Inside(full, root)) throw new IOException("文件路径超出目标目录。");
        return full;
    }
    static void NoLinks(string path) {
        string p = Path.GetFullPath(path);
        for (;;) {
            if ((File.Exists(p) || Directory.Exists(p)) && (File.GetAttributes(p) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("为防止修改目录外的数据，不处理符号链接或目录联接：" + p);
            var parent = Path.GetDirectoryName(p);
            if (String.IsNullOrEmpty(parent) || parent == p) break;
            p = parent;
        }
    }
    // R = runtime replacement; K = add only when absent; P = untouched.
    internal static char Policy(string name) {
        ValidateRelative(name);
        string n = name.ToLowerInvariant();
        if (n == "qt.conf" || n == "sndcpy.bat") return 'K';
        if (n.Contains('/')) {
            string dir = n.Substring(0, n.IndexOf('/'));
            if (new[]{"audio","bearer","iconengines","imageformats","mediaservice","platforms","playlistformats","position","printsupport","styles"}.Contains(dir) && n.EndsWith(".dll")) return 'R';
            if (dir == "resources" && (n.EndsWith(".pak") || n.EndsWith(".dat"))) return 'R';
            if (dir == "translations" && (n.EndsWith(".qm") || n.EndsWith(".pak"))) return 'R';
            if (dir == "licenses" && (n.EndsWith(".txt") || n.EndsWith("/license"))) return 'R';
            return 'P';
        }
        if (n.EndsWith(".dll") || new[]{"qtscrcpy.exe","qtwebengineprocess.exe","adb.exe","scrcpy-server","sndcpy.apk","build-info.json","license-qtscrcpy","license-qtscrcpycore"}.Contains(n)) return 'R';
        if (n.StartsWith("readme-") && n.EndsWith(".md")) return 'R';
        return 'P';
    }
    static void Idle() {
        // Never force-kill a process: an in-memory recording could be lost.
        foreach (var p in Process.GetProcessesByName("QtScrcpy")) {
            using (p) { if (!p.HasExited) throw new IOException("请先保存尚未保存的录制，然后正常退出所有 QtScrcpy 窗口，再重新运行更新。更新器不会强制结束进程。"); }
        }
    }
    public static void ValidateInstallation(string target, string updaterLocation) {
        target = Canonical(target); NoLinks(target);
        if (!Directory.Exists(target) || !File.Exists(Path.Combine(target,"QtScrcpy.exe"))) throw new IOException("请选择旧程序目录中的 QtScrcpy.exe，而不是新版解压目录。");
        if (!String.IsNullOrEmpty(updaterLocation) && (Inside(updaterLocation,target) || Names.Equals(Canonical(updaterLocation),target)))
            throw new IOException("请把更新器放在旧程序目录之外（例如下载目录），再选择旧 QtScrcpy.exe。");
        if (Environment.OSVersion.Platform == PlatformID.Win32NT) {
            if (!Environment.Is64BitOperatingSystem) throw new IOException("此更新仅适用于 Windows 64 位。");
            var drive = new DriveInfo(Path.GetPathRoot(target));
            if (!String.Equals(drive.DriveFormat,"NTFS",StringComparison.OrdinalIgnoreCase)) throw new IOException("此安全覆盖更新器要求本地 NTFS 磁盘；当前目录没有被修改。");
            var v = FileVersionInfo.GetVersionInfo(Path.Combine(target,"QtScrcpy.exe"));
            if (v.FileMajorPart != 0 || v.FileMinorPart != 4 || v.FileBuildPart > 4)
                throw new IOException("仅允许更新 0.4.0—0.4.4 系列，拒绝降级或覆盖无法识别的程序。检测版本：" + v.FileVersion);
            using (var b = new BinaryReader(File.OpenRead(Path.Combine(target,"QtScrcpy.exe")))) {
                if (b.ReadUInt16() != 0x5a4d) throw new IOException("目标不是有效的 Windows 程序。");
                b.BaseStream.Position=0x3c; int off=b.ReadInt32();
                if (off<64 || off>b.BaseStream.Length-6) throw new IOException("程序头无效。");
                b.BaseStream.Position=off;
                if (b.ReadUInt32()!=0x4550 || b.ReadUInt16()!=0x8664) throw new IOException("旧程序必须为 x64，不能覆盖 32 位或其它架构。");
            }
            string info=Path.Combine(target,"build-info.json");
            if (File.Exists(info)) {
                string text=File.ReadAllText(info);
                var match=System.Text.RegularExpressions.Regex.Match(text,"\"version\"\\s*:\\s*\"([^\"]+)\"");
                if (match.Success && match.Groups[1].Value.StartsWith("0.4.4") && match.Groups[1].Value!=Version)
                    throw new IOException("检测到不同或更新的 0.4.4 版本，拒绝把它降级为候选版："+match.Groups[1].Value);
            }
        }
        Idle();
    }
    static Dictionary<string,Item> Snapshot(string root) {
        var result = new Dictionary<string,Item>(Names);
        var stack = new Stack<string>(); stack.Push(root);
        while (stack.Count>0) {
            string dir=stack.Pop(); NoLinks(dir);
            foreach (string f in Directory.GetFileSystemEntries(dir)) {
                NoLinks(f);
                if (Directory.Exists(f)) { stack.Push(f); continue; }
                string rel=f.Substring(root.Length+1).Replace(Path.DirectorySeparatorChar,'/'); ValidateRelative(rel);
                result.Add(rel,new Item{Name=rel,Size=new FileInfo(f).Length,Hash=HashFile(f)});
            }
        }
        return result;
    }
    static void CopyVerified(string source,string dest,string hash) {
        NoLinks(source); NoLinks(dest); Directory.CreateDirectory(Path.GetDirectoryName(dest));
        using (var input=new FileStream(source,FileMode.Open,FileAccess.Read,FileShare.Read))
        using (var output=new FileStream(dest,FileMode.CreateNew,FileAccess.Write,FileShare.None)) {
            input.CopyTo(output); output.Flush(true);
        }
        if (HashFile(dest)!=hash) throw new IOException("复制后校验失败："+source);
        File.SetLastWriteTimeUtc(dest,File.GetLastWriteTimeUtc(source));
    }
    static List<Item> Unpack(Stream zipStream,string stage) {
        var entries=new List<Item>(); var seen=new HashSet<string>(Names);
        using (var zip=new ZipArchive(zipStream,ZipArchiveMode.Read,true)) {
            foreach (var e in zip.Entries) {
                if (!e.FullName.StartsWith(Prefix,StringComparison.Ordinal)) throw new InvalidDataException("更新包目录不符。");
                string name=e.FullName.Substring(Prefix.Length);
                if (name.Length==0) continue;
                if (name.EndsWith("/")) { ValidateRelative(name.TrimEnd('/')); continue; }
                ValidateRelative(name);
                if (!seen.Add(name)) throw new InvalidDataException("更新包含重名文件。");
                if (((e.ExternalAttributes>>16)&0xf000)==0xa000) throw new InvalidDataException("不接受包含符号链接的更新包。");
                if (Policy(name)=='P') continue; // never extract bundled defaults over user data
                string dest=At(stage,name); Directory.CreateDirectory(Path.GetDirectoryName(dest));
                using (var input=e.Open()) using (var output=new FileStream(dest,FileMode.CreateNew,FileAccess.Write,FileShare.None)) {
                    input.CopyTo(output); output.Flush(true);
                }
                if (new FileInfo(dest).Length!=e.Length) throw new InvalidDataException("解压长度不符。");
                entries.Add(new Item{Name=name,Size=e.Length,Hash=HashFile(dest)});
            }
        }
        if (!entries.Any(x=>Names.Equals(x.Name,"QtScrcpy.exe"))) throw new InvalidDataException("更新包缺少 QtScrcpy.exe。");
        return entries;
    }
    static void SaveJournal(string file,string target,string phase,List<Change> changes) {
        var doc=new XDocument(new XElement("update",new XAttribute("format",Format),new XAttribute("target",target),new XAttribute("phase",phase),new XAttribute("version",Version),
            changes.Select(x=>new XElement("file",new XAttribute("name",x.Name),new XAttribute("existed",x.Existed),new XAttribute("old",x.OldHash??""),new XAttribute("new",x.NewHash)))));
        string temp=file+"."+Guid.NewGuid().ToString("N")+".tmp";
        using(var stream=new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None)) { doc.Save(stream); stream.Flush(true); }
        if (File.Exists(file)) File.Replace(temp,file,null); else File.Move(temp,file);
    }
    static XDocument ReadJournal(string file) {
        NoLinks(file);
        var settings=new XmlReaderSettings { DtdProcessing=DtdProcessing.Prohibit,XmlResolver=null,MaxCharactersInDocument=1000000 };
        using(var reader=XmlReader.Create(file,settings)) return XDocument.Load(reader);
    }
    public static string Pending(string target) {
        string root=BackupRoot(target); NoLinks(root);
        if (!Directory.Exists(root)) return null;
        foreach(var dir in Directory.GetDirectories(root).OrderByDescending(x=>x)) {
            string p=Path.Combine(dir,"transaction.xml"); if(!File.Exists(p)) continue;
            var doc=ReadJournal(p);
            string phase=(string)doc.Root.Attribute("phase");
            if(phase!="complete" && phase!="rolled-back") return p;
        }
        return null;
    }
    static void CheckWritable(string file) {
        if (!File.Exists(file)) return;
        NoLinks(file);
        if ((File.GetAttributes(file)&FileAttributes.ReadOnly)!=0) throw new IOException("需替换的程序文件为只读，未开始覆盖："+file);
        try { using(var s=new FileStream(file,FileMode.Open,FileAccess.ReadWrite,FileShare.None)) { } }
        catch(Exception ex) { throw new IOException("文件被占用或不可写："+file+"。请正常关闭相关程序后重试；若是 adb.exe，请先结束相关 ADB 会话。",ex); }
    }
    static Mutex Lock(string target) {
        string key;
        using(var s=new MemoryStream(Encoding.UTF8.GetBytes(target.ToUpperInvariant()))) key=Hash(s);
        var m=new Mutex(false,"QtScrcpySafeUpdate_"+key);
        bool acquired=false;
        try { acquired=m.WaitOne(0); } catch(AbandonedMutexException) { acquired=true; }
        if(!acquired) { m.Dispose(); throw new IOException("此目录已有另一个更新器正在操作。"); }
        return m;
    }
    public static UpdateResult Apply(string target,Stream package,string expectedHash,Action<string> log) {
        target=Canonical(target); NoLinks(target); Idle();
        if(!Directory.Exists(target)||!File.Exists(Path.Combine(target,"QtScrcpy.exe"))) throw new IOException("找不到旧 QtScrcpy.exe。");
        if(Names.Equals(target,Canonical(Path.GetPathRoot(target))) || Path.GetDirectoryName(target)==null) throw new IOException("不能更新磁盘根目录。");
        using(var mutex=Lock(target)) {
            string transaction=null; bool started=false; var changes=new List<Change>();
            try {
                var pending=Pending(target); if(pending!=null) throw new IOException("检测到未完成的更新。请先用『恢复备份』选择："+pending);
                log("核对内置完整程序包 SHA-256…");
                if(!package.CanSeek) throw new IOException("更新包不可定位。");
                package.Position=0; if(Hash(package)!=expectedHash) throw new InvalidDataException("更新包损坏或校验值不符，未修改旧程序。"); package.Position=0;
                log("检查旧目录并计算文件校验值…");
                var snapshot=Snapshot(target);
                string root=BackupRoot(target); NoLinks(root);
                if(Environment.OSVersion.Platform==PlatformID.Win32NT) {
                    long required=snapshot.Values.Sum(x=>x.Size)+350L*1024*1024;
                    if(new DriveInfo(Path.GetPathRoot(target)).AvailableFreeSpace<required) throw new IOException("磁盘空间不足：需要旧目录大小加约 350 MiB 可用空间用于备份及暂存。");
                }
                transaction=Path.Combine(root,DateTime.Now.ToString("yyyyMMdd-HHmmss")+"-"+Guid.NewGuid().ToString("N").Substring(0,8));
                Directory.CreateDirectory(transaction);
                string stage=Path.Combine(transaction,"stage"),before=Path.Combine(transaction,"old-install");
                Directory.CreateDirectory(stage); Directory.CreateDirectory(before);
                log("解压并建立精确程序文件清单；跳过 config 和 keymap 默认文件…");
                var incoming=Unpack(package,stage);
                foreach(var item in incoming) {
                    Item old; snapshot.TryGetValue(item.Name,out old);
                    if(Policy(item.Name)=='K' && old!=null) continue;
                    if(old!=null && old.Hash==item.Hash) continue;
                    NoLinks(At(target,item.Name));
                    if(Directory.Exists(At(target,item.Name))) throw new IOException("目标位置是目录而不是程序文件："+item.Name);
                    changes.Add(new Change{Name=item.Name,Existed=old!=null,OldHash=old==null?"":old.Hash,NewHash=item.Hash});
                }
                // Replace the main executable last; unchanged dependencies are not touched.
                changes=changes.OrderBy(x=>Names.Equals(x.Name,"QtScrcpy.exe")?1:0).ThenBy(x=>x.Name,Names).ToList();
                foreach(var c in changes) CheckWritable(At(target,c.Name));
                log("完整备份旧程序目录（含其中的脚本和配置），并逐文件校验…");
                int index=0;
                foreach(var item in snapshot.Values) {
                    Checkpoint("backup",index++);
                    CopyVerified(At(target,item.Name),At(before,item.Name),item.Hash);
                    File.SetAttributes(At(before,item.Name),File.GetAttributes(At(target,item.Name)) & ~FileAttributes.ReparsePoint);
                }
                var inventory=new XDocument(new XElement("snapshot",snapshot.Values.Select(x=>new XElement("file",new XAttribute("name",x.Name),new XAttribute("sha256",x.Hash)))));
                inventory.Save(Path.Combine(transaction,"old-files.xml"));
                Idle();
                foreach(var c in changes) {
                    string dest=At(target,c.Name);
                    if(c.Existed && (!File.Exists(dest)||HashFile(dest)!=c.OldHash)) throw new IOException("备份期间程序文件发生变化，未开始覆盖："+c.Name);
                    if(!c.Existed && (File.Exists(dest)||Directory.Exists(dest))) throw new IOException("目标出现同名新文件，未开始覆盖："+c.Name);
                    CheckWritable(dest);
                }
                string journal=Path.Combine(transaction,"transaction.xml");
                SaveJournal(journal,target,"installing",changes); started=true;
                log("备份已验证，开始覆盖程序；用户数据不在替换清单内…");
                index=0;
                foreach(var c in changes) {
                    Checkpoint("before-replace",index); Idle();
                    string dest=At(target,c.Name); NoLinks(dest); Directory.CreateDirectory(Path.GetDirectoryName(dest));
                    if(c.Existed && (!File.Exists(dest)||HashFile(dest)!=c.OldHash)) throw new IOException("替换前检测到外部修改："+c.Name);
                    if(c.Existed) File.Replace(At(stage,c.Name),dest,null); else File.Move(At(stage,c.Name),dest);
                    if(HashFile(dest)!=c.NewHash) throw new IOException("更新后程序校验失败："+c.Name);
                    Checkpoint("after-replace",index++);
                }
                log("核对保留文件的内容与更新前一致…"); Checkpoint("verify-data",0);
                var changed=new HashSet<string>(changes.Select(x=>x.Name),Names);
                foreach(var old in snapshot.Values.Where(x=>!changed.Contains(x.Name))) {
                    string f=At(target,old.Name); NoLinks(f);
                    if(!File.Exists(f)||HashFile(f)!=old.Hash) throw new IOException("保留文件被外部修改或删除；停止并回退程序，不用旧数据覆盖它："+old.Name);
                }
                SaveJournal(journal,target,"complete",changes); started=false;
                log("覆盖更新完成。备份保留于："+transaction);
                return new UpdateResult { BackupDirectory=transaction,Replaced=changes.Count(x=>x.Existed),Added=changes.Count(x=>!x.Existed),Preserved=snapshot.Count-changes.Count(x=>x.Existed) };
            }
            catch(Exception ex) {
#if UPDATER_TEST
                if(ex is SimulatedCrash) throw;
#endif
                if(started && transaction!=null) {
                    try { RestoreInternal(Path.Combine(transaction,"transaction.xml"),log); }
                    catch(Exception rollback) { throw new IOException("更新未完成，自动回退也未完成。不要启动混合版本；完整旧目录备份保留在："+transaction+"\r\n更新错误："+ex.Message+"\r\n回退错误："+rollback.Message,ex); }
                    throw new IOException("更新未完成，程序文件已回退；未覆盖脚本。备份："+transaction+"\r\n原因："+ex.Message,ex);
                }
                throw;
            }
            finally { mutex.ReleaseMutex(); }
        }
    }
    public static void Restore(string journal,Action<string> log) {
        var doc=ReadJournal(journal); string target=Canonical((string)doc.Root.Attribute("target"));
        using(var mutex=Lock(target)) { try { RestoreInternal(journal,log); } finally { mutex.ReleaseMutex(); } }
    }
    static void RestoreInternal(string journal,Action<string> log) {
        var doc=ReadJournal(journal); var root=doc.Root;
        if(root==null || root.Name!="update" || (string)root.Attribute("format")!=Format || (string)root.Attribute("version")!=Version) throw new InvalidDataException("不是本更新器的备份记录。");
        string target=Canonical((string)root.Attribute("target")); NoLinks(target); Idle();
        string txn=Canonical(Path.GetDirectoryName(journal));
        if(!Names.Equals(Canonical(Path.GetDirectoryName(txn)),Canonical(BackupRoot(target)))) throw new InvalidDataException("备份目录与原安装目录不匹配；不要移动备份。");
        string before=Path.Combine(txn,"old-install"); var changes=new List<Change>(); var seen=new HashSet<string>(Names);
        foreach(var node in root.Elements("file")) {
            string name=(string)node.Attribute("name"); ValidateRelative(name);
            bool existed=(bool)node.Attribute("existed");
            if(!seen.Add(name) || Policy(name)=='P' || (Policy(name)=='K' && existed)) throw new InvalidDataException("恢复清单不得覆盖用户数据。");
            string old=(string)node.Attribute("old"),next=(string)node.Attribute("new");
            if(!System.Text.RegularExpressions.Regex.IsMatch(next??"","^[a-f0-9]{64}$") || (existed&&!System.Text.RegularExpressions.Regex.IsMatch(old??"","^[a-f0-9]{64}$"))) throw new InvalidDataException("恢复校验值无效。");
            changes.Add(new Change{Name=name,Existed=existed,OldHash=old,NewHash=next});
        }
        // Validate ALL inputs before reverting ANY file. Preserve post-update user edits.
        foreach(var c in changes) {
            string dest=At(target,c.Name); NoLinks(dest);
            if(c.Existed) {
                string backup=At(before,c.Name); NoLinks(backup);
                if(!File.Exists(backup)||HashFile(backup)!=c.OldHash) throw new IOException("备份损坏，拒绝恢复："+c.Name);
                if(!File.Exists(dest)) throw new IOException("当前程序文件已被外部删除，需人工核对："+c.Name);
            }
            if(Directory.Exists(dest)) throw new IOException("恢复目标位置变成目录："+c.Name);
            if(File.Exists(dest)) {
                string current=HashFile(dest);
                if(current!=c.NewHash && (!c.Existed||current!=c.OldHash)) throw new IOException("该程序文件在更新后被再次修改；为避免覆盖它，停止恢复："+c.Name);
                if(current==c.NewHash) CheckWritable(dest);
            }
        }
        log("恢复旧程序文件；保留当前脚本和配置，不把它们退回旧版本…");
        foreach(var c in changes.AsEnumerable().Reverse()) {
            string dest=At(target,c.Name); NoLinks(dest);
            if(!File.Exists(dest)) continue;
            string current=HashFile(dest);
            if(c.Existed && current==c.OldHash) continue;
            if(current!=c.NewHash) throw new IOException("恢复过程中出现外部修改："+c.Name);
            if(c.Existed) {
                string temp=Path.Combine(txn,"restore-"+Guid.NewGuid().ToString("N")+".tmp");
                CopyVerified(At(before,c.Name),temp,c.OldHash);
                File.Replace(temp,dest,null);
                if(HashFile(dest)!=c.OldHash) throw new IOException("回退校验失败："+c.Name);
            } else File.Delete(dest); // only a tracked NEW runtime file with matching hash
        }
        SaveJournal(journal,target,"rolled-back",changes);
        log("旧程序文件已恢复，脚本数据保留。备份仍在："+txn);
    }
}
}
