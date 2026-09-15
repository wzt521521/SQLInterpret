Option Explicit
Dim shell, fs, folder, python, script
Set shell = CreateObject("WScript.Shell")
Set fs = CreateObject("Scripting.FileSystemObject")
folder = fs.GetParentFolderName(WScript.ScriptFullName)
python = fs.BuildPath(folder, ".venv\Scripts\pythonw.exe")
script = fs.BuildPath(folder, "start_desktop.pyw")
If Not fs.FileExists(python) Then
    MsgBox "Python environment missing: " & python, 16, "MiniDBMS"
    WScript.Quit 1
End If
shell.CurrentDirectory = folder
shell.Run Chr(34) & python & Chr(34) & " " & Chr(34) & script & Chr(34), 0, False
