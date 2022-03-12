
# 簡易インストーラ

下記プラグイン用のインストール補助ツールです。

- [x264guiEx](https://github.com/rigaya/x264guiEx) 3.xx 以降
- [x265guiEx](https://github.com/rigaya/x265guiEx) 4.xx 以降
- [svtAV1guiEx](https://github.com/rigaya/svtAV1guiEx) 1.xx 以降
- [QSVEnc](https://github.com/rigaya/QSVEnc) 7.xx 以降
- [NVEnc](https://github.com/rigaya/NVEnc) 6.xx 以降
- [VCEEnc](https://github.com/rigaya/VCEEnc) 7.xx 以降

## 構成

### auo_setup.exe
簡易インストーラ本体です。

VC runtime, .NET Frameworkのインストールを行います。

### auo_setup.auf
インストール補助用のダミープラグインです。

VC runtimeや.NET Frameworkがインストールされているかをチェックし、
されていなければauo_setup.exeを呼んでインストールを行います。

### check_vc.dll
VC runtimeに依存しているため、このdllをロードできればVC runtimeはインストール済み、できなければインストールの必要があるとわかります。

### check_dotnet.dll
check_vc.dllと同様ですが、.NET Frameworkをチェックします。

### guiExBase
auo_setup.exe, auo_setup.aufの両者で用いる関数群です。
