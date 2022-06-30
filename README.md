# Wio-Terminal-Kmoni

![wio-terminal-kmoni](/sample.jpg)

このプログラムはWio Terminalを使った強震モニタのウォッチャです。

国立研究開発法人防災科学技術研究所の『強震モニタ』のデータを定期的に確認し、予想震度情報の取得に成功した場合、Wio Terminalでビープ音を鳴らし、LCDに予想震度情報、震源・P波・S波リアルタイム震度情報を一定時間表示するプログラムです。

以下で配布されているM5Stack用プログラムをWio Terminalに移植したものです。

http://www.ria-lab.com/archives/3339

M5Stack版とできるだけ同じ動作になるようにしていますが、一部ハードウェアの制限により動作が異なる部分があります。  

## 開発環境 Environment

  - PlatformIO


## 使用ライブラリ Library dependencies

  - [arduino-libraries/NTPClient](https://github.com/arduino-libraries/NTPClient)
  - [bitbank2/AnimatedGIF](https://github.com/bitbank2/AnimatedGIF)
  - [LovyanGFX](https://github.com/Lovyan03/LovyanGFX)
  - [mbed-kazushi2008/HTTPClient](https://os.mbed.com/users/kazushi2008/code/HTTPClient/)
  - [cyrusbuilt/SAMCrashMonitor](https://github.com/cyrusbuilt/SAMCrashMonitor)


## ライセンス License

原作者の方のライセンスを踏襲します。基本的に無保証です。改変、再配布はご自由にどうぞ。

NYSLライセンス  
This software is distributed under the license of NYSL.  
http://www.kmonos.net/nysl/
