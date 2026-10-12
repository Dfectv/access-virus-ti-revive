// vtimsipatch.c - runs under Wine (Linux installer): makes a copy of the user's Access "Virus TI Software Suite"
// MSI install without its kernel drivers, firmware updater and RunOnce entry, like tirevive-access.ps1 on Windows
// (those custom actions get the condition "0"). Uses Windows Installer's own database API, so no host tools needed.
// usage: wine vtimsipatch.exe <copy of the Access .msi>      (the file is modified in place)
#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    static const char *skip[] = {"ConnectDevicesDialog", "PrepareDriverInstall", "InstallDrivers",
                                 "FinishDriverInstall", "InstallRunOnceEntry", "ApplyOptimization", "UpdateFirmware"};
    static const char *tables[] = {"InstallExecuteSequence", "InstallUISequence"};
    if (argc < 2) {
        fprintf(stderr, "usage: vtimsipatch <file.msi>\n");
        return 2;
    }
    MSIHANDLE db;
    UINT r = MsiOpenDatabaseA(argv[1], MSIDBOPEN_TRANSACT, &db);
    if (r) {
        fprintf(stderr, "open %s: error %u\n", argv[1], r);
        return 1;
    }
    int changed = 0;
    for (int t = 0; t < 2; t++)
        for (int a = 0; a < (int)(sizeof skip / sizeof *skip); a++) {
            char q[256];
            snprintf(q, sizeof q, "UPDATE `%s` SET `Condition` = '0' WHERE `Action` = '%s'", tables[t], skip[a]);
            MSIHANDLE v;
            if (MsiDatabaseOpenViewA(db, q, &v) == ERROR_SUCCESS) {
                if (MsiViewExecute(v, 0) == ERROR_SUCCESS)
                    changed++;
                MsiViewClose(v);
                MsiCloseHandle(v);
            }
        }
    r = MsiDatabaseCommit(db);
    MsiCloseHandle(db);
    printf("%s: %d updates, commit %u\n", argv[1], changed, r);
    return r ? 1 : 0;
}
