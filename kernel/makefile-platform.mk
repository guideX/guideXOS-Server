# OS-specific directory commands. Windows uses PowerShell so these recipes are
# valid under both cmd.exe and Git's POSIX sh, which mingw32-make may select.
ifeq ($(OS),Windows_NT)
    MKDIR = powershell.exe -NoProfile -NonInteractive -Command "New-Item -ItemType Directory -Force -Path '$(subst /,\,$1)' | Out-Null"
    RM = powershell.exe -NoProfile -NonInteractive -Command "if (Test-Path -LiteralPath '$(subst /,\,$1)') { Remove-Item -LiteralPath '$(subst /,\,$1)' -Force -Recurse }"
else
    MKDIR = mkdir -p $1
    RM = rm -rf $1
endif
