using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using QscUpdate;

// Exercises the REAL old and new packages, not only mocked runtime files.
class UpdaterUpgradeTest {
    static void Check(bool value,string message) { if(!value)throw new Exception(message); }
    static void Write(string root,string relative,string text) {
        string path=Path.Combine(root,relative); Directory.CreateDirectory(Path.GetDirectoryName(path));
        File.WriteAllText(path,text,System.Text.Encoding.UTF8);
    }
    static void Main(string[] args) {
        string root=Path.Combine(Path.GetTempPath(),"QtScrcpy-real-upgrade-"+Guid.NewGuid().ToString("N"));
        try {
            Check(args.Length==2,"supply new payload and old 0.4.4 payload");
            Check(Updater.HashFile(args[0])==Updater.PayloadHash,"new payload SHA-256 mismatch");
            Check(Updater.HashFile(args[1])=="1e966081f8f5ce03a1ca518c5b7c008b1cc7da7d1de4ffbcfd4ac4a612213e41","old payload SHA-256 mismatch");
            Directory.CreateDirectory(root);
            ZipFile.ExtractToDirectory(args[1],root);
            string target=Directory.GetDirectories(root).Single();
            Write(target,"scripts\\我的操作.qsmacro.json","original-macro-数据");
            Write(target,"keymap\\tiktok.json","my-own-keymap-not-a-bundled-example");
            Write(target,"config\\userdata.ini","my-own-history");
            Write(target,"custom-script.py","print('unchanged')");
            string[] protectedFiles={"scripts\\我的操作.qsmacro.json","keymap\\tiktok.json","config\\userdata.ini","config\\config.ini","custom-script.py","qt.conf","sndcpy.bat"};
            var hashes=protectedFiles.Where(p=>File.Exists(Path.Combine(target,p))).ToDictionary(p=>p,p=>Updater.HashFile(Path.Combine(target,p)));
            string oldExeHash=Updater.HashFile(Path.Combine(target,"QtScrcpy.exe"));
            Check(FileVersionInfo.GetVersionInfo(Path.Combine(target,"QtScrcpy.exe")).FileBuildPart==4,"wrong baseline app");
            Updater.ValidateInstallation(target,Path.Combine(root,"updater.exe"));
            UpdateResult result;
            using(var stream=File.OpenRead(args[0]))result=Updater.Apply(target,stream,Updater.PayloadHash,Console.WriteLine);
            Check(result.Replaced>0,"did not replace real runtime files");
            Check(FileVersionInfo.GetVersionInfo(Path.Combine(target,"QtScrcpy.exe")).FileBuildPart==5,"app not upgraded to 0.4.5");
            Check(Updater.HashFile(Path.Combine(target,"QtScrcpy.exe"))!=oldExeHash,"old main program remains");
            foreach(var pair in hashes)Check(Updater.HashFile(Path.Combine(target,pair.Key))==pair.Value,"data changed during upgrade: "+pair.Key);
            Write(target,"scripts\\我的操作.qsmacro.json","edited-after-upgrade");
            Write(target,"scripts\\新增操作.qsmacro.json","new-after-upgrade");
            Updater.Restore(Path.Combine(result.BackupDirectory,"transaction.xml"),Console.WriteLine);
            Check(Updater.HashFile(Path.Combine(target,"QtScrcpy.exe"))==oldExeHash,"old program not restored");
            Check(File.ReadAllText(Path.Combine(target,"scripts\\我的操作.qsmacro.json"))=="edited-after-upgrade","rollback destroyed later script edit");
            Check(File.ReadAllText(Path.Combine(target,"scripts\\新增操作.qsmacro.json"))=="new-after-upgrade","rollback deleted new script");
            foreach(var pair in hashes.Where(p=>!p.Key.StartsWith("scripts\\")))Check(Updater.HashFile(Path.Combine(target,pair.Key))==pair.Value,"rollback changed other data: "+pair.Key);
            Console.WriteLine("PASS real-0.4.4-to-0.4.5-and-rollback-preserves-later-scripts");
        } catch(Exception ex) { Console.Error.WriteLine("FAIL real-upgrade: "+ex);Environment.ExitCode=1; }
        finally { if(Directory.Exists(root))Directory.Delete(root,true); }
    }
}
