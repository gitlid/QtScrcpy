using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using QscUpdate;
class FailureTests {
    static void Check(bool yes,string text){if(!yes)throw new Exception(text);}
    static MemoryStream Package(){var s=new MemoryStream();using(var z=new ZipArchive(s,ZipArchiveMode.Create,true)){foreach(var x in new[]{"QtScrcpy.exe","Qt5Core.dll","a.dll","platforms/qwindows.dll","build-info.json"})using(var w=new StreamWriter(z.CreateEntry("QtScrcpy-keymapper-0.4.5-rc.1-win64/"+x).Open()))w.Write("new-"+x);}s.Position=0;return s;}
    static void Main(){int total=0;for(int mode=0;mode<2;mode++)for(int boundary=0;boundary<5;boundary++){
        string root=Path.Combine(Path.GetTempPath(),"Qsc-Faults-"+Guid.NewGuid().ToString("N")),target=Path.Combine(root,"old");Directory.CreateDirectory(target);
        File.WriteAllText(Path.Combine(target,"QtScrcpy.exe"),"old-exe");File.WriteAllText(Path.Combine(target,"Qt5Core.dll"),"old-dll");File.WriteAllText(Path.Combine(target,"keep.qsmacro.json"),"my-script");
        int point=boundary,kind=mode;Updater.Fault=(p,n)=>{if(p=="after-replace"&&n==point){if(kind==1)throw new Updater.SimulatedCrash();throw new IOException("injected");}};
        bool threw=false;
        try{using(var s=Package()){string h=Updater.Hash(s);s.Position=0;Updater.Apply(target,s,h,delegate{});}}catch{threw=true;}finally{Updater.Fault=null;}
        try{
            Check(threw,"fault not triggered");if(mode==1){string pending=Updater.Pending(target);Check(pending!=null,"no crash journal");Updater.Restore(pending,delegate{});}
            Check(File.ReadAllText(Path.Combine(target,"QtScrcpy.exe"))=="old-exe","executable lost");Check(File.ReadAllText(Path.Combine(target,"Qt5Core.dll"))=="old-dll","dll lost");Check(File.ReadAllText(Path.Combine(target,"keep.qsmacro.json"))=="my-script","macro lost");
            Check(!File.Exists(Path.Combine(target,"a.dll"))&&!File.Exists(Path.Combine(target,"platforms/qwindows.dll"))&&!File.Exists(Path.Combine(target,"build-info.json")),"added files not reverted");Check(Updater.Pending(target)==null,"journal remains pending");
            total++;Console.WriteLine("PASS fault-mode="+mode+" after-file="+boundary);
        }finally{Directory.Delete(root,true);}
    }Console.WriteLine("RESULT fault-matrix passed="+total+" failed=0");}
}
