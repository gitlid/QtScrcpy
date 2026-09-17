using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Threading.Tasks;
using System.Windows.Forms;
[assembly: AssemblyTitle("QtScrcpy 保留脚本覆盖更新器")]
[assembly: AssemblyDescription("Offline 0.4.3-rc.1 update with verified backups and script-preserving rollback")]
[assembly: AssemblyVersion("1.0.0.0")]
[assembly: AssemblyFileVersion("1.0.0.0")]
namespace QscUpdate {
internal static class Program {
    internal static Stream Payload() { return Assembly.GetExecutingAssembly().GetManifestResourceStream("qsc-payload.zip"); }
    [STAThread]
    static int Main(string[] args) {
        if(args.Length==1 && args[0]=="--check-payload") {
            try { using(var p=Payload()) return p!=null && Updater.Hash(p)==Updater.PayloadHash ? 0 : 2; } catch { return 3; }
        }
        Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new UpdateWindow()); return 0;
    }
}
internal sealed class UpdateWindow : Form {
    readonly TextBox target=new TextBox(), log=new TextBox();
    readonly Button browse=new Button(), update=new Button(), restore=new Button(), openBackup=new Button();
    readonly CheckBox saved=new CheckBox();
    readonly ProgressBar progress=new ProgressBar();
    bool working; string lastBackup;
    public UpdateWindow() {
        Text="QtScrcpy 0.4.3-rc.1 · 保留脚本覆盖更新"; ClientSize=new Size(780,585);
        MinimumSize=new Size(700,580); StartPosition=FormStartPosition.CenterScreen;
        Font=new Font("Microsoft YaHei UI",9F); AutoScaleMode=AutoScaleMode.Dpi;
        var panel=new TableLayoutPanel { Dock=DockStyle.Fill,ColumnCount=1,RowCount=8,Padding=new Padding(18) };
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute,40));
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute,96));
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute,26));
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute,38));
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute,38));
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute,48));
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute,20));
        panel.RowStyles.Add(new RowStyle(SizeType.Percent,100));
        Controls.Add(panel);
        panel.Controls.Add(new Label { Text="在原目录更新，脚本路径不变",Font=new Font(Font.FontFamily,16F,FontStyle.Bold),Dock=DockStyle.Fill },0,0);
        panel.Controls.Add(new Label { Text="保留已有 config、keymap、宏脚本及其它自定义文件。\r\nAppData、外部脚本目录和环境变量不修改；不重置应用绑定。\r\n完整备份旧程序目录后才替换程序；回退时也不覆盖脚本数据。\r\n仅适用于本地 NTFS 上的 0.4.0—0.4.3 x64；无需联网。",Dock=DockStyle.Fill },0,1);
        panel.Controls.Add(new Label { Text="选择你目前实际使用的旧 QtScrcpy.exe：",Dock=DockStyle.Fill },0,2);
        var row=new TableLayoutPanel { Dock=DockStyle.Fill,ColumnCount=2,RowCount=1 };
        row.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100)); row.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,110));
        target.ReadOnly=true; target.Dock=DockStyle.Fill;
        browse.Text="选择旧程序…"; browse.Dock=DockStyle.Fill;
        browse.Click+=(s,e)=> { using(var dialog=new OpenFileDialog { Filter="QtScrcpy 主程序|QtScrcpy.exe",Title="请选择原来使用的 QtScrcpy.exe",CheckFileExists=true }) {
            if(dialog.ShowDialog(this)==DialogResult.OK) target.Text=Path.GetDirectoryName(dialog.FileName);
        }};
        row.Controls.Add(target,0,0);row.Controls.Add(browse,1,0);panel.Controls.Add(row,0,3);
        saved.Text="我已保存需要保留的录制，并正常退出旧 QtScrcpy。"; saved.Dock=DockStyle.Fill;
        panel.Controls.Add(saved,0,4);
        var buttons=new FlowLayoutPanel { Dock=DockStyle.Fill,WrapContents=false };
        update.Text="备份并覆盖更新"; update.Width=170;update.Height=35;
        restore.Text="恢复备份 / 中断恢复…";restore.Width=195;restore.Height=35;
        openBackup.Text="打开备份目录";openBackup.Width=145;openBackup.Height=35;openBackup.Enabled=false;
        buttons.Controls.Add(update);buttons.Controls.Add(restore);buttons.Controls.Add(openBackup);panel.Controls.Add(buttons,0,5);
        progress.Dock=DockStyle.Fill;panel.Controls.Add(progress,0,6);
        log.Multiline=true;log.ReadOnly=true;log.ScrollBars=ScrollBars.Vertical;log.Dock=DockStyle.Fill;log.BackColor=SystemColors.Window;
        panel.Controls.Add(log,0,7);
        update.Click+=StartUpdate;restore.Click+=StartRestore;
        openBackup.Click+=(s,e)=> { if(Directory.Exists(lastBackup)) Process.Start("explorer.exe",lastBackup); };
        FormClosing+=(s,e)=> { if(working) { e.Cancel=true;MessageBox.Show(this,"正在备份或替换文件，请勿关闭或断电。",Text,MessageBoxButtons.OK,MessageBoxIcon.Information); } };
        Append("本工具未作数字签名。请使用可信来源文件并核对 SHA-256。\r\n备份位于旧目录旁的 <原目录名>.qsc-update-backups，完成后不要立即删除。\r\n未保存到文件的录制不能通过更新保留。程序不会自动启动新版。\r\n这次只改变更新方式；应用功能版本仍为 0.4.3-rc.1。");
    }
    void Append(string message) {
        if(InvokeRequired) { BeginInvoke(new Action<string>(Append),message);return; }
        log.AppendText(DateTime.Now.ToString("HH:mm:ss")+"  "+message+Environment.NewLine);
    }
    void Busy(bool on) {
        working=on;browse.Enabled=update.Enabled=restore.Enabled=saved.Enabled=!on;
        progress.Style=on?ProgressBarStyle.Marquee:ProgressBarStyle.Blocks;
        openBackup.Enabled=!on && lastBackup!=null && Directory.Exists(lastBackup);
    }
    async void StartUpdate(object sender,EventArgs ev) {
        if(!saved.Checked || String.IsNullOrWhiteSpace(target.Text)) { MessageBox.Show(this,"先选择旧程序并确认已经保存录制、退出旧版。",Text);return; }
        string dir=target.Text;
        try { Updater.ValidateInstallation(dir,Application.ExecutablePath); }
        catch(Exception ex) { MessageBox.Show(this,ex.Message,"未开始更新",MessageBoxButtons.OK,MessageBoxIcon.Warning);return; }
        string backup=Updater.BackupRoot(dir);
        if(MessageBox.Show(this,"将覆盖更新：\r\n"+dir+"\r\n\r\n自动备份位置：\r\n"+backup+"\r\n\r\n已有脚本和配置不会用新版默认值覆盖。\r\n备份仅包含旧程序目录；其它位置的数据不修改，也不纳入此备份。\r\n确认继续？",Text,MessageBoxButtons.OKCancel,MessageBoxIcon.Question,MessageBoxDefaultButton.Button2)!=DialogResult.OK)return;
        Busy(true);
        try {
            var result=await Task.Run(()=> { using(var payload=Program.Payload()) {
                if(payload==null) throw new IOException("内置程序包缺失，请重新取得完整更新器。");
                return Updater.Apply(dir,payload,Updater.PayloadHash,Append);
            }});
            lastBackup=result.BackupDirectory;
            Append("完成：替换 "+result.Replaced+" 个程序文件；新增 "+result.Added+" 个程序文件；原样保留并校验 "+result.Preserved+" 个已有文件。");
            MessageBox.Show(this,"覆盖更新完成。请从原快捷方式或原目录启动 QtScrcpy。\r\n\r\n脚本文件和配置保留，备份位置：\r\n"+result.BackupDirectory,"更新完成",MessageBoxButtons.OK,MessageBoxIcon.Information);
        } catch(Exception ex) { lastBackup=backup;Append(ex.Message);MessageBox.Show(this,ex.Message,"更新未完成",MessageBoxButtons.OK,MessageBoxIcon.Error); }
        finally { Busy(false); }
    }
    async void StartRestore(object sender,EventArgs ev) {
        string file;
        using(var dialog=new OpenFileDialog { Filter="更新事务记录|transaction.xml",Title="选择原目录旁备份文件夹内的 transaction.xml",CheckFileExists=true }) {
            if(lastBackup!=null && Directory.Exists(lastBackup)) dialog.InitialDirectory=lastBackup;
            else if(!String.IsNullOrEmpty(target.Text) && Directory.Exists(Updater.BackupRoot(target.Text))) dialog.InitialDirectory=Updater.BackupRoot(target.Text);
            if(dialog.ShowDialog(this)!=DialogResult.OK)return;file=dialog.FileName;
        }
        if(MessageBox.Show(this,"只恢复该次更新替换的程序文件；保留现在的脚本和配置。\r\n请先保存录制并正常退出 QtScrcpy。\r\n\r\n确认恢复？",Text,MessageBoxButtons.OKCancel,MessageBoxIcon.Question,MessageBoxDefaultButton.Button2)!=DialogResult.OK)return;
        Busy(true);
        try { await Task.Run(()=>Updater.Restore(file,Append));lastBackup=Path.GetDirectoryName(file);MessageBox.Show(this,"旧程序文件已恢复；当前脚本数据未回退。",Text); }
        catch(Exception ex) { Append(ex.Message);MessageBox.Show(this,ex.Message,"恢复未完成",MessageBoxButtons.OK,MessageBoxIcon.Error); }
        finally { Busy(false); }
    }
}
}
