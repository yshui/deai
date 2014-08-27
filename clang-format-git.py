#!/usr/bin/python


import os
import subprocess
import sys


def getGitHead():
    if subprocess.Popen(['git', 'rev-parse', '--verify', 'HEAD'],
        stdout=subprocess.PIPE).returncode:
        return '4b825dc642cb6eb9a060e54bf8d69288fbee4904'
    else:
        return 'HEAD'


def getEditedFiles():
    Head = getGitHead()
    DiffIndex = subprocess.Popen(
        ['git', 'diff-index', '--diff-filter=ACMR', '--name-only', Head],
        stdout=subprocess.PIPE)
    DiffIndexRet = DiffIndex.stdout.read()
    return DiffIndexRet.split('\n')


def isFormattable(File):
    Extension = os.path.splitext(File)[1]
    for Ext in ['.h', '.cpp', '.hpp', '.c', '.cc', '.hh', '.cxx', '.hxx']:
        if Ext == Extension:
            return True
    return False


def formatFile(File):
    ClangFormat = subprocess.Popen(
        ['clang-format', '-style=file', File],
        stdout=subprocess.PIPE)
    return ClangFormat.stdout.read()


def requiresFormat(FileName, FormatContent):
    File = open(FileName)
    Content = File.read()
    File.close()
    if FormatContent == Content:
        return False
    return True


EditedFiles = getEditedFiles()
FormatResults = {}
for FileName in EditedFiles:
    if isFormattable(FileName):
        FormatResults[FileName] = formatFile(FileName)


ReturnCode = 0
for Result in FormatResults.items():
    FileName = Result[0]
    if requiresFormat(FileName, Result[1]):
        Message = "'" + FileName + "' requires formatting, run 'make format'"
        print(Message)
        ReturnCode = 1


sys.exit(ReturnCode)
