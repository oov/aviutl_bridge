# bridge.auf / bridge.dll

拡張編集で Lua から外部プログラムを使って処理をしやすくするためのプラグインです。

外部プログラムは stdin / stdout を使って通信するものを作成する必要があります。

## 注意事項

bridge.auf / bridge.dll は無保証で提供されます。  
bridge.auf / bridge.dll を使用したこと及び使用しなかったことによるいかなる損害について、開発者は何も責任を負いません。

これに同意できない場合、あなたは bridge.auf / bridge.dll を使用することができません。

# インストール／アンインストール

1. `bridge.auf` を `aviutl.exe` と同じ場所か、`plugins` フォルダーの中へ入れます。
2. `bridge.dll` を `exedit.auf` と同じフォルダーにある `script` フォルダーの中に入れます。  
`script` フォルダーが見つからない場合は作成してください。

アンインストールは導入したファイルを削除するだけで完了です。

# 使い方

拡張編集上でスクリプト制御などから以下のようにすると、`C:\your\binary.exe` が起動し、標準入力から `stdin data` が渡され、標準出力したデータが `stdout_data` に届きます。

```lua
-- 単純な例
local stdout_data = require("bridge").call("C:\\your\\binary.exe", "stdin data");
-- スペースを含むパスを渡す場合などはダブルクォートで囲む
local stdout_data = require("bridge").call("\"C:\\your\\binary.exe\"", "stdin data");
-- 必要であれば引数を渡すこともできる
local stdout_data = require("bridge").call("C:\\your\\binary.exe param", "stdin data");
```

以下は対応するプログラムのサンプルです。

```c
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <windows.h>

// 共有メモリ上のヘッダ部
struct share_mem_header {
    uint32_t header_size;
    uint32_t body_size;
    uint32_t version;
    uint32_t width;
    uint32_t height;
};

// obj.getpixeldata 由来
struct pixel {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
};

int main(){
    // 入出力をバイナリーモードに切り替えておく
    _setmode( _fileno(stdin), _O_BINARY);
    _setmode( _fileno(stdout), _O_BINARY);

    for (int i = 0; i < 100; ++i) {
        // local stdout_data = require("bridge").call("C:\\your\\binary.exe", "stdin data");
        // Lua で上記のように bridge.dll 経由でアクセスすると、まず入力データ "stdin data" が stdin で届く
        // それを読み取った上で適切に処理して戻り値を stdout で返すまでが一度の処理
        // stdout で返したデータは call の戻り値として文字列で取得できる
        // stdout への出力後は維持すべきデータが特になければそのままプログラムを終了しても構わないし、
        // 再度 stdin の入力を待っても構わない
        // 次回呼び出し時にプログラムが既に終了していた場合はまた起動される

        // 入出力のデータ形式はどちらも [ int32_t len ][ char data[len] ]

        // 入力データの処理
        {
            char buf[1024];
            int32_t len;
            if (fread(&len, sizeof(len), 1, stdin) != 1) {
                OutputDebugStringA("cannot read input data size");
                return 1;
            }
            if (len >= 1024) {
                OutputDebugStringA("input data too big");
                return 1;
            }
            if (len > 0) {
                if (fread(buf, 1, len, stdin) != len) {
                    OutputDebugStringA("cannot read input data");
                    return 1;
                }
                // Lua から送られたデータを OutputDebugString で表示
                buf[len] = '\0';
                OutputDebugStringA(buf);
            }
        }

        // require("bridge").call("C:\\your\\binary.exe", "send data", "rw");
        // Lua 側で上記のように call の第三引数に "r" や "w" や "rw" や "rwp" などを付けると、
        // obj.getpixeldata / obj.putpixeldata を利用したピクセルデータのやり取りができる
        //   r  - 読み取りのみ、拡張編集から外部exe(これ)にピクセルデータを渡す
        //   w  - 書き込みのみ、外部exe(これ)から拡張編集にピクセルデータを反映
        //   p  - obj.getpixeldata や obj.putpixeldata を内部で呼び出さずに、
        //        第四引数以降に「画像データ」「幅」「高さ」を直接渡すことで処理を行う
        {
            // ピクセルデータは FileMappingObject にあり、環境変数を参照すると名前が取れる
            char fmo_name[32];
            GetEnvironmentVariableA("BRIDGE_FMO", fmo_name, 32);
            HANDLE fmo = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, fmo_name);
            if (!fmo) {
                OutputDebugStringA("cannot open fmo");
                return 1;
            }
            void *view = MapViewOfFile(fmo, FILE_MAP_WRITE, 0, 0, 0);
            if (!view) {
                OutputDebugStringA("cannot map fmo");
                CloseHandle(fmo);
                return 1;
            }
            // FileMappingObject は [ HEADER ][ BODY ] という構造になっており、
            // header_size 分飛ばすとデータ本体がある
            struct share_mem_header *h = view;
            int width = h->width;
            int height = h->height;
            // 将来的に bridge.auf / bridge.dll のバージョンが上がった際に
            // ヘッダ部のサイズがこのプログラムのコンパイル時よりも大きくなる可能性があるので、
            // view + sizeof(struct share_mem_header) ではなく必ず view + h->header_size にする
            struct pixel *px = view + h->header_size;

            // 上の方に行くほど透明になるようにデータを書き換え
            // call で "r" だけを指定していた場合は書き換えても画面に反映されないので注意
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x, ++px)
                {
                    px->a *= (double)(y) / (double)(height);
                }
            }

            UnmapViewOfFile(view);
            CloseHandle(fmo);
        }

        // 出力データの処理
        // ここで返したデータは Lua 側で call の戻り値として取得できる
        {
            char buf[1024];
            int32_t len = sprintf(buf, "Hello world %d!", i);
            if (fwrite(&len, sizeof(len), 1, stdout) != 1) {
                OutputDebugStringA("cannot write output data size");
                return 1;
            }
            if (fwrite(buf, 1, len, stdout) != len) {
                OutputDebugStringA("cannot write output data");
                return 1;
            }
            fflush(stdout);
        }
    }
    return 0;
}
```

## 更新履歴

### v0.8 2021-03-10

- call の第三引数用フラグとして新しく `p` を追加

### v0.7 2021-03-09

- exe パスとしてディレクトリーが渡されるとクラッシュしていたのを修正

### v0.6 2021-03-09

- 依存ライブラリの更新

### v0.5 2020-10-22

- スレッド用のライブラリを変更

### v0.4 2020-10-22

- 開発環境を変更

### v0.3 2019-08-31

- データの読み書き処理を改善

### v0.2 2019-08-14

- 小さいメモリリークを修正

### v0.1 2019-08-14

- 初版

## Credits

bridge.auf / bridge.dll is made possible by the following open source softwares.

### AviUtl Plugin SDK

http://spring-fragrance.mints.ne.jp/aviutl/

The MIT License

Copyright (c) 1999-2012 Kenkun

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

### Lua

http://www.lua.org/

Copyright (C) 1994-2003 Tecgraf, PUC-Rio.  All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

### stb_ds.h

http://nothings.org/stb_ds

Copyright (c) 2019 Sean Barrett

Permission is hereby granted, free of charge, to any person obtaining a copy of 
this software and associated documentation files (the "Software"), to deal in 
the Software without restriction, including without limitation the rights to 
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies 
of the Software, and to permit persons to whom the Software is furnished to do 
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all 
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
SOFTWARE.

### threads.h

https://cgit.freedesktop.org/mesa/mesa/tree/include/c11/threads.h

C11 <threads.h> emulation library

(C) Copyright yohhoy 2012.
Distributed under the Boost Software License, Version 1.0.

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare [[derivative work]]s of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
