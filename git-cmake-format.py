#!/usr/bin/python


import os
import subprocess
import sys


Git='git'
ClangFormat='clang-format'


def getGitHead():
    RevParse = subprocess.Popen([Git, 'rev-parse', '--verify', 'HEAD'],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    RevParse.communicate()
    if RevParse.returncode:
        return '4b825dc642cb6eb9a060e54bf8d69288fbee4904'
    else:
        return 'HEAD'


def getEditedFiles():
    Head = getGitHead()
    DiffIndex = subprocess.Popen(
        [Git, 'diff-index', '--diff-filter=ACMR', '--name-only', Head],
        stdout=subprocess.PIPE)
    DiffIndexRet = DiffIndex.stdout.read()
    return DiffIndexRet.split('\n')


def isFormattable(File):
    Extension = os.path.splitext(File)[1]
    for Ext in ['.h', '.cpp', '.hpp', '.c', '.cc', '.hh', '.cxx', '.hxx']:
        if Ext == Extension:
            return True
    return False


def formatFile(FileName, InPlace):
    if InPlace:
        subprocess.Popen([ClangFormat, '-style=file', '-i', FileName])
        return ""
    else:
        ClangFormatRet = subprocess.Popen(
            [ClangFormat, '-style=file', FileName], stdout=subprocess.PIPE)
        return ClangFormatRet.stdout.read()


def requiresFormat(FileName, FormatContent):
    File = open(FileName)
    Content = File.read()
    File.close()
    if FormatContent == Content:
        return False
    return True


def printUsageAndExit():
    print("Usage: " + sys.argv[0] + " [--pre-commit|--cmake] " +
          "[<path/to/git>] [<path/to/clang-format]")
    sys.exit(1)


if __name__ == "__main__":
    if 2 > len(sys.argv):
        printUsageAndExit()

    if "--pre-commit" == sys.argv[1]:
        InPlace = False
    elif "--cmake" == sys.argv[1]:
        InPlace = True
    else:
        printUsageAndExit()

    if 3 <= len(sys.argv):
        if "git" in sys.argv[2]:
            Git = sys.argv[2]
        elif "clang-format" in sys.argv[2]:
            ClangFormat = sys.argv[2]
        else:
            printUsageAndExit()

    if 4 <= len(sys.argv):
        if "git" in sys.argv[3]:
            Git = sys.argv[3]
        elif "clang-format" in sys.argv[3]:
            ClangFormat = sys.argv[3]
        else:
            printUsageAndExit()

    EditedFiles = getEditedFiles()
    FormatResults = {}
    for FileName in EditedFiles:
        if isFormattable(FileName):
            FormatResults[FileName] = formatFile(FileName, InPlace)

    ReturnCode = 0
    if not InPlace:
        for Result in FormatResults.items():
            FileName = Result[0]
            if requiresFormat(FileName, Result[1]):
                print("'" + FileName +
                      "' must be formatted, run the cmake target 'format'")
                ReturnCode = 1

    sys.exit(ReturnCode)
