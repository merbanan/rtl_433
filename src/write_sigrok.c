/** @file
    Sigrok Pulseview format writer.

    Copyright (C) 2020 by Christian Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif
#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

#include "fatal.h"
#include "write_sigrok.h"

void write_sigrok(char const *filename, unsigned samplerate, unsigned probes, unsigned analogs, char const *labels[])
{
    // e.g. uses channels
    // U8:LOGIC:logic-1-1
    // F32:I:analog-1-4-1
    // F32:Q:analog-1-5-1
    // F32:AM:analog-1-6-1
    // F32:FM:analog-1-7-1

    // probe1=FRAME
    // probe2=ASK
    // probe3=FSK
    // analog4=I
    // analog5=Q
    // analog6=AM
    // analog7=FM

    // create version tag
    FILE *fp = fopen("version", "w");
    if (!fp) {
        perror("creating Sigrok \"version\" file");
        return;
    }
    fprintf(fp, "2");
    fclose(fp);

    // create meta data
    fp = fopen("metadata", "w");
    if (!fp) {
        perror("creating Sigrok \"metadata\" file");
        return;
    }
    fprintf(fp,
            "[device 1]\n"
            "samplerate=%u kHz\n"
            "capturefile=logic-1\n"
            "unitsize=1\n"
            "total probes=%u\n"
            "total analog=%u\n",
            samplerate / 1000, probes, analogs);
    if (labels) {
        char const **label = labels;
        for (unsigned i = 1; i <= probes; ++i)
            fprintf(fp, "probe%u=%s\n", i, *label++);
        for (unsigned i = probes + 1; i <= probes + analogs; ++i)
            fprintf(fp, "analog%u=%s\n", i, *label++);
    }
    else {
        for (unsigned i = 1; i <= probes; ++i)
            fprintf(fp, "probe%u=L%u\n", i, i);
        for (unsigned i = probes + 1; i <= probes + analogs; ++i)
            fprintf(fp, "analog%u=A%u\n", i, i);
    }

    // EOF
    fclose(fp);

#ifdef _WIN32
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    char cmd_line[MAX_PATH] = "";
    strcat_s(cmd_line, MAX_PATH, "7z.exe");
    strcat_s(cmd_line, MAX_PATH, " a");
    strcat_s(cmd_line, MAX_PATH, " -bb0 ");
    strcat_s(cmd_line, MAX_PATH, " -sdel ");
    strcat_s(cmd_line, MAX_PATH, " -tzip ");
    strcat_s(cmd_line, MAX_PATH, filename);
    strcat_s(cmd_line, MAX_PATH, " version");
    strcat_s(cmd_line, MAX_PATH, " metadata");

    if (probes) {
        strcat_s(cmd_line, MAX_PATH, " logic-1-1");
    }

    char str_buf[64];
    for (unsigned i = probes + 1; i <= probes + analogs; ++i) {
        snprintf(str_buf, 64, " analog-1-%u-1", i);
        strcat_s(cmd_line, MAX_PATH, str_buf);
    }

    // Start the child process.
    if (CreateProcess(NULL,  // No module name (use command line)
                cmd_line,     // Command line
                NULL,        // Process handle not inheritable
                NULL,        // Thread handle not inheritable
                FALSE,       // Set handle inheritance to FALSE
                0,           // No creation flags
                NULL,        // Use parent's environment block
                NULL,        // Use parent's starting directory
                &si,         // Pointer to STARTUPINFO structure
                &pi)         // Pointer to PROCESS_INFORMATION structure
    ) {
        // Wait until child process exits.
        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exit_code;

        if (FALSE == GetExitCodeProcess(pi.hProcess, &exit_code)) {
            perror("GetExitCodeProcess() failure");
        }

        // Close process and thread handles.
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exit_code != 0) {
            perror("7z.exe execution failed");
            return;
        }
    }
    else {
        perror("CreateProcess for 7z.exe failed.");
        return;
    }

#else
    char *argv[30] = {0};
    int arg        = 0;
    argv[arg++]    = "zip";
    argv[arg++]    = (char *)filename; // "out.sr"
    argv[arg++]    = "version";
    argv[arg++]    = "metadata";

    if (probes) {
        argv[arg++] = "logic-1-1";
    }

    char **argv_analog = &argv[arg];
    char str_buf[64];
    for (unsigned i = probes + 1; i <= probes + analogs; ++i) {
        snprintf(str_buf, sizeof(str_buf), "analog-1-%u-1", i);
        char* dup = strdup(str_buf);
        if (!dup) {
          FATAL_STRDUP("write_sigrok()");
        }
        argv[arg++] = dup;
    }

    int status = 0;
    pid_t pid = fork();
    if (pid < 0) {
        perror("forking zip");
        return;
    }
    else if (pid == 0) {
        // child process because return value zero
        execvp(argv[0], argv);
        // execvp() returns only on error
        for (int i = 0; i < arg; ++i)
            fprintf(stderr, "%s ", argv[i]);
        fprintf(stderr, "\n");
        perror("execvp");
        exit(1);
    }
    else {
        // parent process because return value non-zero
        wait(&status);
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status)) {
                fprintf(stderr, "zip exited with status: %d\n", WEXITSTATUS(status));
            }
        }
    }

    // rm version metadata logic-1-1 analog-1-4-1 analog-1-5-1 analog-1-6-1 analog-1-7-1
    if (unlink("version")) {
        perror("unlinking Sigrok \"version\" file");
    }
    if (unlink("metadata")) {
        perror("unlinking Sigrok \"metadata\" file");
    }

    if (probes) {
        if (unlink("logic-1-1")) {
            perror("unlinking Sigrok \"logic-1-1\" file");
        }
    }
    for (unsigned i = 0; i < analogs; ++i) {
        if (unlink(argv_analog[i])) {
            perror("unlinking Sigrok \"analog-1-N-1\" file");
        }
        free(argv_analog[i]);
    }
#endif // !_WIN32
}

void open_pulseview(char const *filename)
{
#ifdef _WIN32
    fprintf(stderr, "Opening Pulseview not implemented for win32\n");
#else
    char *argv[9] = {0};
    int arg       = 0;
    char *abspath = realpath(filename, NULL);
#ifdef __APPLE__
    argv[arg++] = "open";
    argv[arg++] = "-b";
    argv[arg++] = "org.sigrok.PulseView";
    argv[arg++] = "--fresh";
    argv[arg++] = "--new";
    argv[arg++] = "--args";
    argv[arg++] = "-i";
    argv[arg++] = (char *)abspath;
#else
    argv[arg++] = "pulseview";
    argv[arg++] = "-i";
    argv[arg++] = abspath;
#endif

    fprintf(stderr, "Opening Pulseview...\n");
    int status = 0;
    pid_t pid = fork();
    if (pid < 0) {
        perror("forking pulseview");
        return;
    }
    else if (pid == 0) {
        // child process because return value zero
        execvp(argv[0], argv);
        // execvp() returns only on error
        for (int i = 0; i < arg; ++i)
            fprintf(stderr, "%s ", argv[i]);
        fprintf(stderr, "\n");
        perror("execvp");
        exit(1);
    }
    else {
        // parent process because return value non-zero
        wait(&status);
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status)) {
                fprintf(stderr, "pulseview open exited with status: %d\n", WEXITSTATUS(status));
            }
        }
    }
    free(abspath);
#endif
}
