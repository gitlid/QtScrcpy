using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Xml.Linq;
using QscUpdate;
class UpdaterTests {
    static int pass,fail;
    static void Assert(bool b,string msg) { if(!b)throw new Exception(msg); }
    static void Reject(Action a) { bool rejected=false;try{a();}catch{rejected=true;}Assert(rejected,"expected rejection"); }
    static void Run(string name,Action test) { try { test();pass++;Console.WriteLine("PASS "+name); }catch(Exception e){fail++;Console.WriteLine("FAIL "+name+": "+e);}finally{Updater.Fault=null;} }
    static void Put(string root,string rel,string text) { string f=Path.Combine(root,rel.Replace('/',Path.DirectorySeparatorChar));Directory.CreateDirectory(Path.GetDirectoryName(f));File.WriteAllText(f,text,Encoding.UTF8); }
    static string Get(string root,string rel) { return File.ReadAllText(Path.Combine(root,rel.Replace('/',Path.DirectorySeparatorChar)),Encoding.UTF8); }
    static MemoryStream Zip(IDictionary<string,string> entries) {
        var s=new MemoryStream();
        using(var z=new ZipArchive(s,ZipArchiveMode.Create,true)) foreach(var x in entries) using(var w=new StreamWriter(z.CreateEntry("QtScrcpy-keymapper-0.4.3-rc.1-win64/"+x.Key).Open(),Encoding.UTF8)) w.Write(x.Value);
        s.Position=0;return s;
    }
    sealed class Fixture : IDisposable {
        internal string Root=Path.Combine(Path.GetTempPath(),"QscUpdater-tests-"+Guid.NewGuid().ToString("N")),Target,Outside;
        internal Fixture(){ Target=Path.Combine(Root,"旧程序 with spaces");Outside=Path.Combine(Root,"AppData","QtScrcpy");Directory.CreateDirectory(Target);Put(Target,"QtScrcpy.exe","old-exe");Put(Target,"Qt5Core.dll","old-core");Put(Target,"config/config.ini","my-settings");Put(Target,"config/userdata.ini","my-history");Put(Target,"keymap/tiktok.json","custom-keymap");Put(Target,"scripts/动作.qsmacro.json","my-macro");Put(Target,"action-macro.qsmacro.json","root-macro");Put(Target,"custom/start.bat","my-script");Put(Target,"qt.conf","custom-runtime-config");Put(Target,"sndcpy.bat","custom-audio-script");Put(Outside,"app-profiles/phone.json","application-binding");Put(Outside,"rotation-backups/phone.json","rotation-backup"); }
        internal MemoryStream Package(){return Zip(new Dictionary<string,string>{{"QtScrcpy.exe","new-exe"},{"Qt5Core.dll","new-core"},{"new-runtime.dll","new-dependency"},{"platforms/qwindows.dll","platform-dependency"},{"config/config.ini","DEFAULT-DO-NOT-COPY"},{"keymap/tiktok.json","DEFAULT-DO-NOT-COPY"},{"keymap/new-example.json","DEFAULT-DO-NOT-COPY"},{"qt.conf","DEFAULT-DO-NOT-COPY"},{"sndcpy.bat","DEFAULT-DO-NOT-COPY"},{"build-info.json","new-build-metadata"}});}
        internal UpdateResult Apply(){using(var s=Package()){string hash=Updater.Hash(s);s.Position=0;return Updater.Apply(Target,s,hash,delegate{});}}
        internal void Data(){ Assert(Get(Target,"config/config.ini")=="my-settings","settings overwritten");Assert(Get(Target,"config/userdata.ini")=="my-history","history overwritten");Assert(Get(Target,"keymap/tiktok.json")=="custom-keymap","keymap overwritten");Assert(Get(Target,"scripts/动作.qsmacro.json")=="my-macro","macro overwritten");Assert(Get(Target,"action-macro.qsmacro.json")=="root-macro","root macro overwritten");Assert(Get(Target,"custom/start.bat")=="my-script","unknown script overwritten");Assert(Get(Target,"qt.conf")=="custom-runtime-config","qt.conf overwritten");Assert(Get(Target,"sndcpy.bat")=="custom-audio-script","sndcpy script overwritten");Assert(Get(Outside,"app-profiles/phone.json")=="application-binding","external profile touched");Assert(Get(Outside,"rotation-backups/phone.json")=="rotation-backup","external backup touched");Assert(!File.Exists(Path.Combine(Target,"keymap/new-example.json")),"default keymap introduced");}
        public void Dispose(){foreach(var f in Directory.GetFiles(Root,"*",SearchOption.AllDirectories))File.SetAttributes(f,FileAttributes.Normal);Directory.Delete(Root,true);}
    }
    static void Main(string[] args){
        Run("replace-runtime-and-preserve-scripts-configs-external-data",()=>{using(var f=new Fixture()){var r=f.Apply();f.Data();Assert(Get(f.Target,"QtScrcpy.exe")=="new-exe","exe not replaced");Assert(Get(f.Target,"Qt5Core.dll")=="new-core","dll not replaced");Assert(r.Replaced==2,"wrong replacement count");Assert(Get(Path.Combine(r.BackupDirectory,"old-install"),"scripts/动作.qsmacro.json")=="my-macro","no backup of script");Assert(Get(Path.Combine(r.BackupDirectory,"old-install"),"QtScrcpy.exe")=="old-exe","no backup of program");}});
        Run("idempotent-same-version-update",()=>{using(var f=new Fixture()){f.Apply();var r=f.Apply();f.Data();Assert(r.Replaced==0&&r.Added==0,"unchanged files replaced");}});
        Run("rollback-keeps-newly-edited-and-newly-created-scripts",()=>{using(var f=new Fixture()){var r=f.Apply();Put(f.Target,"scripts/动作.qsmacro.json","edited-after-update");Put(f.Target,"scripts/new.qsmacro.json","created-after-update");Updater.Restore(Path.Combine(r.BackupDirectory,"transaction.xml"),delegate{});Assert(Get(f.Target,"QtScrcpy.exe")=="old-exe","exe not restored");Assert(Get(f.Target,"scripts/动作.qsmacro.json")=="edited-after-update","rollback overwrote edited macro");Assert(Get(f.Target,"scripts/new.qsmacro.json")=="created-after-update","rollback deleted new macro");Assert(!File.Exists(Path.Combine(f.Target,"new-runtime.dll")),"tracked new dependency not removed");}});
        Run("backup-failure-does-not-change-installation",()=>{using(var f=new Fixture()){Updater.Fault=(p,n)=>{if(p=="backup"&&n==2)throw new IOException("simulated backup failure");};Reject(()=>f.Apply());Assert(Get(f.Target,"QtScrcpy.exe")=="old-exe","backup failure changed exe");Assert(Get(f.Target,"Qt5Core.dll")=="old-core","backup failure changed dll");f.Data();}});
        Run("failure-during-replacement-rolls-back",()=>{using(var f=new Fixture()){Updater.Fault=(p,n)=>{if(p=="after-replace"&&n==2)throw new IOException("simulated copy error");};Reject(()=>f.Apply());Assert(Get(f.Target,"QtScrcpy.exe")=="old-exe","rollback exe mismatch");Assert(Get(f.Target,"Qt5Core.dll")=="old-core","rollback dll mismatch");Assert(!File.Exists(Path.Combine(f.Target,"new-runtime.dll")),"rollback left new binary");Assert(Updater.Pending(f.Target)==null,"successful rollback still pending");f.Data();}});
        Run("interrupted-update-blocks-new-update-and-can-recover",()=>{using(var f=new Fixture()){Updater.Fault=(p,n)=>{if(p=="after-replace"&&n==2)throw new Updater.SimulatedCrash();};Reject(()=>f.Apply());Updater.Fault=null;string pending=Updater.Pending(f.Target);Assert(pending!=null,"missing recovery journal");Reject(()=>f.Apply());Updater.Restore(pending,delegate{});Assert(Get(f.Target,"Qt5Core.dll")=="old-core","interrupted rollback failed");f.Data();}});
        Run("restore-refuses-corrupted-backup-before-any-change",()=>{using(var f=new Fixture()){var r=f.Apply();Put(Path.Combine(r.BackupDirectory,"old-install"),"QtScrcpy.exe","corrupt");Reject(()=>Updater.Restore(Path.Combine(r.BackupDirectory,"transaction.xml"),delegate{}));Assert(Get(f.Target,"Qt5Core.dll")=="new-core","partial restore on corrupt backup");Assert(Get(f.Target,"QtScrcpy.exe")=="new-exe","partial restore on corrupt backup");f.Data();}});
        Run("restore-refuses-a-later-program-update",()=>{using(var f=new Fixture()){var r=f.Apply();Put(f.Target,"QtScrcpy.exe","later-program");Reject(()=>Updater.Restore(Path.Combine(r.BackupDirectory,"transaction.xml"),delegate{}));Assert(Get(f.Target,"Qt5Core.dll")=="new-core","partial restore on changed binary");Assert(Get(f.Target,"QtScrcpy.exe")=="later-program","later binary overwritten");}});
        Run("restore-journal-cannot-overwrite-user-data",()=>{using(var f=new Fixture()){var r=f.Apply();string p=Path.Combine(r.BackupDirectory,"transaction.xml");var d=XDocument.Load(p);d.Root.Elements("file").First().SetAttributeValue("name","config/config.ini");d.Save(p);Reject(()=>Updater.Restore(p,delegate{}));f.Data();Assert(Get(f.Target,"QtScrcpy.exe")=="new-exe","partial restore from malicious journal");}});
        Run("malformed-package-hash-rejected-before-writing",()=>{using(var f=new Fixture())using(var s=f.Package()){Reject(()=>Updater.Apply(f.Target,s,new string('0',64),delegate{}));Assert(!Directory.Exists(Updater.BackupRoot(f.Target)),"bad package created transaction");f.Data();}});
        Run("zip-path-traversal-rejected",()=>{using(var f=new Fixture())using(var s=Zip(new Dictionary<string,string>{{"../escape.dll","bad"},{"QtScrcpy.exe","new"}})){string h=Updater.Hash(s);s.Position=0;Reject(()=>Updater.Apply(f.Target,s,h,delegate{}));Assert(Get(f.Target,"QtScrcpy.exe")=="old-exe","unsafe zip changed exe");f.Data();}});
        Run("zip-case-duplicate-rejected",()=>{using(var f=new Fixture())using(var s=Zip(new Dictionary<string,string>{{"QtScrcpy.exe","new"},{"QTSCRCPY.EXE","ambiguous"}})){string h=Updater.Hash(s);s.Position=0;Reject(()=>Updater.Apply(f.Target,s,h,delegate{}));f.Data();}});
        Run("runtime-name-directory-collision-stops-update",()=>{using(var f=new Fixture()){Directory.CreateDirectory(Path.Combine(f.Target,"new-runtime.dll"));Reject(()=>f.Apply());Assert(Get(f.Target,"Qt5Core.dll")=="old-core","collision changed dll");f.Data();}});
        Run("readonly-user-script-kept-and-backed-up",()=>{using(var f=new Fixture()){string p=Path.Combine(f.Target,"scripts/动作.qsmacro.json");File.SetAttributes(p,FileAttributes.ReadOnly);f.Apply();f.Data();Assert((File.GetAttributes(p)&FileAttributes.ReadOnly)!=0,"readonly metadata lost");}});
        Run("readonly-runtime-rejected-before-overwrite",()=>{using(var f=new Fixture()){File.SetAttributes(Path.Combine(f.Target,"Qt5Core.dll"),FileAttributes.ReadOnly);Reject(()=>f.Apply());Assert(Get(f.Target,"QtScrcpy.exe")=="old-exe","readonly preflight changed exe");f.Data();}});
        Run("add-only-runtime-settings-created-only-when-missing",()=>{using(var f=new Fixture()){File.Delete(Path.Combine(f.Target,"qt.conf"));var r=f.Apply();Assert(Get(f.Target,"qt.conf")=="DEFAULT-DO-NOT-COPY","missing runtime config not added");Updater.Restore(Path.Combine(r.BackupDirectory,"transaction.xml"),delegate{});Assert(!File.Exists(Path.Combine(f.Target,"qt.conf")),"added runtime config not reverted");}});
        Run("external-edit-during-update-is-not-overwritten-by-rollback",()=>{using(var f=new Fixture()){Updater.Fault=(p,n)=>{if(p=="verify-data")Put(f.Target,"scripts/动作.qsmacro.json","external-edit");};Reject(()=>f.Apply());Assert(Get(f.Target,"QtScrcpy.exe")=="old-exe","program not reverted on external edit");Assert(Get(f.Target,"scripts/动作.qsmacro.json")=="external-edit","external edit overwritten");}});
        Run("missing-target-program-rejected",()=>{using(var f=new Fixture()){File.Delete(Path.Combine(f.Target,"QtScrcpy.exe"));Reject(()=>f.Apply());Assert(!Directory.Exists(Updater.BackupRoot(f.Target)),"missing target modified filesystem");}});
        foreach(string path in new[]{"../a.dll","/a.dll","a\\b.dll","a:b.dll","a/./b.dll","a//b.dll","NUL.dll","COM1.txt","dir/CON/file","a./b.dll","a /b.dll","a\tb.dll"}){
            string value=path;Run("reject-path-"+value,()=>Reject(()=>Updater.ValidateRelative(value)));
        }
        foreach(string path in new[]{"config/config.ini","config/userdata.ini","keymap/tiktok.json","macros/test.json","scripts/test.ps1","custom.qsmacro.json","custom.py","resources/my-script.json","app-profiles/phone.json","rotation-backups/phone.json"}){
            string value=path;Run("preserve-policy-"+value,()=>Assert(Updater.Policy(value)=='P',"user path permitted for replacement"));
        }
        if(Environment.OSVersion.Platform==PlatformID.Win32NT){
            Run("windows-locked-runtime-stops-before-overwrite",()=>{using(var f=new Fixture()){using(var held=new FileStream(Path.Combine(f.Target,"Qt5Core.dll"),FileMode.Open,FileAccess.Read,FileShare.Read)){Reject(()=>f.Apply());}Assert(Get(f.Target,"QtScrcpy.exe")=="old-exe","locked runtime changed exe");f.Data();}});
            Run("windows-junction-refused-no-external-write",()=>{using(var f=new Fixture()){string link=Path.Combine(f.Target,"linked-data");var psi=new ProcessStartInfo("cmd.exe","/c mklink /J \""+link+"\" \""+f.Outside+"\""){UseShellExecute=false,CreateNoWindow=true};using(var p=Process.Start(psi)){p.WaitForExit();Assert(p.ExitCode==0,"junction test setup failed");}try{Reject(()=>f.Apply());Assert(Get(f.Target,"QtScrcpy.exe")=="old-exe","junction changed program");Assert(Get(f.Outside,"app-profiles/phone.json")=="application-binding","junction modified external data");}finally{Directory.Delete(link);} }});
            Run("windows-invalid-pe-rejected",()=>{using(var f=new Fixture()){Reject(()=>Updater.ValidateInstallation(f.Target,Path.Combine(f.Root,"Updater.exe")));}});
        }
        if(args.Length>0){
            Run("real-release-package-preserves-scripts-and-supports-rollback",()=>{using(var f=new Fixture()){
                using(var zip=new ZipArchive(File.OpenRead(args[0]),ZipArchiveMode.Read)){
                    var e=zip.Entries.First(x=>x.FullName.EndsWith("/QtScrcpy.exe"));using(var input=e.Open())using(var output=File.Create(Path.Combine(f.Target,"QtScrcpy.exe")))input.CopyTo(output);
                }
                Updater.ValidateInstallation(f.Target,Path.Combine(f.Root,"Updater.exe"));
                string exeHash=Updater.HashFile(Path.Combine(f.Target,"QtScrcpy.exe"));
                UpdateResult r;using(var input=File.OpenRead(args[0]))r=Updater.Apply(f.Target,input,Updater.PayloadHash,delegate{});
                f.Data();Assert(Updater.HashFile(Path.Combine(f.Target,"QtScrcpy.exe"))==exeHash,"real main executable changed unexpectedly");
                Put(f.Target,"scripts/动作.qsmacro.json","real-package-new-script");Updater.Restore(Path.Combine(r.BackupDirectory,"transaction.xml"),delegate{});
                Assert(Get(f.Target,"Qt5Core.dll")=="old-core","real payload rollback failed");Assert(Get(f.Target,"scripts/动作.qsmacro.json")=="real-package-new-script","real payload rollback lost script");
            }});
        }
        Console.WriteLine("RESULT passed="+pass+" failed="+fail);
        Environment.ExitCode=fail==0?0:1;
    }
}
