using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

internal static class KernOSLauncher
{
    [STAThread]
    private static void Main()
    {
        string project = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, ".."));
        string qemu = @"C:\Program Files\qemu\qemu-system-x86_64.exe";
        try
        {
            var start = new ProcessStartInfo(qemu);
            start.WorkingDirectory = project;
            start.UseShellExecute = false;
            start.CreateNoWindow = true;
            start.RedirectStandardError = true;
            start.RedirectStandardOutput = true;
            start.Arguments = "-name KernOS -m 128 -no-reboot -monitor none " +
                "-display gtk,gl=off " +
                "-serial file:build/interactive-serial.log " +
                "-drive format=raw,file=build/kernos.img,if=ide " +
                "-device ich9-ahci,id=ahci " +
                "-drive if=none,id=files,format=raw,file=build/ahcidisk.img " +
                "-device ide-hd,drive=files,bus=ahci.0";
            string errorPath = Path.Combine(project, "build", "interactive-qemu.stderr");
            string outputPath = Path.Combine(project, "build", "interactive-qemu.stdout");
            int exitCode;
            using (var errors = new StreamWriter(errorPath, false))
            using (var output = new StreamWriter(outputPath, false))
            using (var vm = new Process())
            {
                errors.AutoFlush = true;
                output.AutoFlush = true;
                vm.StartInfo = start;
                vm.ErrorDataReceived += delegate(object sender, DataReceivedEventArgs data)
                { if (data.Data != null) errors.WriteLine(data.Data); };
                vm.OutputDataReceived += delegate(object sender, DataReceivedEventArgs data)
                { if (data.Data != null) output.WriteLine(data.Data); };
                vm.Start();
                File.WriteAllText(Path.Combine(project, "build", "interactive-qemu.pid"), vm.Id.ToString());
                vm.BeginErrorReadLine();
                vm.BeginOutputReadLine();
                vm.WaitForExit();
                exitCode = vm.ExitCode;
            }
            if (exitCode != 0)
            {
                string details = File.ReadAllText(errorPath);
                if (details.Length > 2000) details = details.Substring(details.Length - 2000);
                MessageBox.Show("QEMU exited with code 0x" + unchecked((uint)exitCode).ToString("X8") +
                    ".\n\n" + details + "\n\nLog: " + errorPath, "KernOS launch failed");
            }
        }
        catch (Exception error)
        {
            MessageBox.Show(error.Message, "KernOS launch failed");
        }
    }
}
