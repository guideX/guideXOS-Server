using System;
using System.Diagnostics;
using System.IO;
using System.Text;

namespace GuideXos.C188
{
    public sealed class LiveProcessCapture : IDisposable
    {
        private readonly object _errorLock = new object();
        private readonly string _stdoutPath;
        private readonly string _stderrPath;
        private string _captureError;

        public Process NativeProcess { get; }

        public string CaptureError
        {
            get
            {
                lock (_errorLock) return _captureError;
            }
        }

        public static string ReadAllTextShared(string path)
        {
            using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete))
            using (var reader = new StreamReader(stream, Encoding.UTF8, true))
            {
                return reader.ReadToEnd();
            }
        }

        public LiveProcessCapture(string executable, string workingDirectory, string[] arguments,
            string stdoutPath, string stderrPath)
        {
            if (File.Exists(stdoutPath) || File.Exists(stderrPath))
                throw new IOException("Refusing to overwrite process capture output.");

            _stdoutPath = stdoutPath;
            _stderrPath = stderrPath;
            _captureError = String.Empty;
            File.WriteAllText(_stdoutPath, String.Empty, new UTF8Encoding(false));
            File.WriteAllText(_stderrPath, String.Empty, new UTF8Encoding(false));

            var startInfo = new ProcessStartInfo
            {
                FileName = executable,
                WorkingDirectory = workingDirectory,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };
            foreach (var argument in arguments) startInfo.ArgumentList.Add(argument);

            NativeProcess = new Process { StartInfo = startInfo, EnableRaisingEvents = true };
            NativeProcess.OutputDataReceived += OnOutputDataReceived;
            NativeProcess.ErrorDataReceived += OnErrorDataReceived;
            if (!NativeProcess.Start()) throw new InvalidOperationException("The captured process did not start.");
            NativeProcess.BeginOutputReadLine();
            NativeProcess.BeginErrorReadLine();
        }

        private void OnOutputDataReceived(object sender, DataReceivedEventArgs args)
        {
            if (args.Data != null) AppendLine(_stdoutPath, args.Data);
        }

        private void OnErrorDataReceived(object sender, DataReceivedEventArgs args)
        {
            if (args.Data != null) AppendLine(_stderrPath, args.Data);
        }

        private void AppendLine(string path, string line)
        {
            try
            {
                File.AppendAllText(path, line + Environment.NewLine, new UTF8Encoding(false));
            }
            catch (Exception exception) when (exception is IOException || exception is UnauthorizedAccessException)
            {
                lock (_errorLock) _captureError = exception.GetType().Name + ": " + exception.Message;
            }
        }

        public void Dispose()
        {
            NativeProcess.Dispose();
        }
    }
}
