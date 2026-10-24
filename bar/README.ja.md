# wm2tj-bar — wm2tj 用のトップバー

[English](README.md) | 日本語

wm2 をもとにしたウィンドウマネージャー wm2tj と組み合わせて使う、単体のトップバー。C + Xlib (Xft・Shape)。ウィンドウマネージャーとは別プロセスで、EWMH のプロパティだけでやりとりする (wm2tj 側の改造は不要)。xfce4-panel の置き換え。

```
[■ mylaptop ◤] [Terminal] [Firefox] [ファイル]   [1][2][3]  ▣▮  ▤▮  ▂▄▆█  [あ]  ᛒ ◁) [電池]85%  12:34
 └ タブ ┘     └ 起動中のアプリ ┘                  CPU メモリ Wi-Fi 入力 BT 音量 バッテリー 時計
```

![screenshot](screenshot.png)

- **左端**: wm2tj のウィンドウの横にあるタブを、同じ太さ (`tab_width`)・色・縁取り・ボタン・斜めの切り口で横にしたもの。ボタン (■) がメニュー。文字はホスト名 (`tab_label`)、長さは文字に合わせて短く
- **その右 (地は黒)**: 今のデスクトップで起動中のアプリ (`_NET_CLIENT_LIST`)。前面のアプリはグレーの単色に、タブの色 (青) の細い線で囲む (`task_gloss` `task_style`。タブと同じ色にすると左端のタブと続いて見えるので、既定では分けている)。最小化中は灰色
  - クリックで前面に出す。前面のものをクリックすると最小化、もう一度で戻る
  - 数が多いと幅が縮む (1 つ最大 200px)。マウスを乗せると題名の全文が出る
  - `_NET_WM_STATE_SKIP_TASKBAR` のウィンドウは出さない
- **右側**: 仮想デスクトップ (番号の四角、クリック / ホイールで切替) → CPU → メモリ → システムトレイ → Wi-Fi → 入力メソッド → Bluetooth → 音量 → バッテリー → 時計
  - CPU・メモリ: アイコン + 縦のバー (下から使用率、90% 以上で赤)
  - 入力メソッド: 今のものの文字 (Mozc なら `あ`、Pinyin なら `拼`、キーボードなら `jp`・`us`)。入力メソッドが入っている間は色付き。fcitx5・IBus・XKB の配列に対応。下の「入力メソッド」
  - Wi-Fi: 電波の 4 本の棒 (強さで点灯数が変わる)。有線ならネットワークのアイコン、未接続は灰色の棒 + 赤い ×。クリックで下の「Wi-Fi メニュー」
  - Bluetooth: ルーン文字のマーク。オフは灰色、機器がつながっている間は青。クリックで下の「Bluetooth メニュー」。アダプターが無い機械では出ない
  - 音量: スピーカーと最大 3 本の波。ミュート中は赤い ×。ホイールで音量、クリックで下の「音」のメニュー。サウンドサーバーが無ければ出ない
  - バッテリー: 電池のアイコン (残量で塗り、15% 以下で赤、充電中は稲妻) + %。電池が無い機械では出ない。クリックで下の「電源まわり」のバッテリーメニュー
  - アイコンにマウスを乗せると数値の吹き出し (例「メモリ 3.2 / 15.5 GB (41%)」「Wi-Fi 78% (wlp2s0)」)。時計に乗せると日付
- CPU・メモリ・Wi-Fi・Bluetooth・バッテリーは `/proc`・`/sys`・カーネルから直接読む。外部コマンドは使わない (時計・CPU・メモリは 1 秒、ほかは 5 秒ごと)。音量と入力メソッドは読める場所が無いので、`wpctl` / `pactl` に 5 秒ごと、`fcitx5-remote` か `ibus` に 1 秒ごとに聞く (1 回 5〜20 ミリ秒)。XKB の配列の切り替えはイベントで届く
- 電源まわり (明るさキー・省電力モード・画面オフ) は下の「電源まわり」

## ■ メニュー

- 上に **お気に入り**。アプリを右クリック →「お気に入りに追加 / 削除」。保存先 `~/.config/wm2tj-bar/favorites` (1 行 1 つの .desktop ファイル名)
  - `sync_wm2tj yes` (設定 GUI の「wm2tj とメニューを同期する」) で、wm2tj の右クリックメニューと同じ `~/.config/wm2tj/favorites` を読み書きする。どちらで足したり外したりしても両方に出る
    - そのファイルがまだ無ければ自分のお気に入り (`~/.config/wm2tj-bar/favorites`) を出し、最初の変更で共有ファイルを作る (wm2tj の `wm2-menu-gen` も、無ければバーのお気に入りから作る)
    - 変えるたびに `wm2tj_menu_command` を走らせて wm2tj 側のメニューを作り直す。既定は `wm2-menu-gen --hup` (PATH → `~/.local/bin` → `/usr/local/bin`) を実行する。wm2tj は次にメニューを開いたときに読み直す (古い wm2tj には SIGHUP)。wm2tj の `wm2-menu-sync.path` (systemd --user) がお気に入りのファイルを見張っているときは何もしない (同じことをするので)
  - そのファイルの `"名前" コマンド` の行もお気に入りとして出し、選ぶとそのコマンドを実行する。書き直すときもその行と頭のコメントはそのまま残す
- **アプリケーション** ▶ カテゴリ別 (アクセサリ / インターネット / オフィス / グラフィックス / マルチメディア / 開発 / 教育 / 科学 / ゲーム / システム / 設定 / その他)
  - `~/.local/share/applications` → `$XDG_DATA_DIRS/applications` (既定 `/usr/local/share` `/usr/share`) の .desktop。同じ名前のファイルは前のものが優先 (ローカルの `Hidden=true` でシステムのものを隠せる)
  - `NoDisplay` / `Hidden` / `TryExec` が無いもの / `OnlyShowIn`・`NotShowIn` (`$XDG_CURRENT_DESKTOP` と照合) は除外
  - 名前は今の言語の `Name[ll_CC]` → `Name[ll]` → 言語パックの翻訳 (`X-GNOME-Gettext-Domain` / `X-Ubuntu-Gettext-Domain`) → `Name`
  - `Exec` の `%f %u` などは取り除いて実行。`Terminal=true` は `terminal` 設定の端末で開く
  - 多すぎて画面に入らないカテゴリは複数列になる
- **セッション** ▶ ロック / ログアウト / サスペンド / 再起動… / シャットダウン…。再起動とシャットダウンは確認を出す (既定はキャンセル側)
- **バーの設定…**: 下の「設定 GUI」。`wm2tj-bar-settings` が無ければ出さない
- キーボード: ↑↓ で選択、→ / Enter でサブメニュー、← で戻る、Enter で実行、Esc で閉じる

## Wi-Fi メニュー

Wi-Fi のアイコンをクリックすると、近くのネットワークの一覧が出る (NetworkManager を `nmcli` で操作)。

- 同じ名前のものは 1 つにまとめて、強い順。接続中のものに ✓、右に電波の棒、鍵付きなら錠前
- 選ぶと接続する。保存済みのネットワークと鍵なしはそのまま、**新しい鍵付きはバーがパスワードを聞く** (nm-applet などは要らない)
  - パスワードは `nmcli --ask` に標準入力で渡す (コマンドラインには載せないので `ps` に出ない)。使い終わったら消す
  - 入力欄: Backspace / Ctrl+U で消去、Enter で接続、Esc でやめる。「パスワードを表示」で文字が見える
  - つながらなかったら、もう一度パスワードを聞く (鍵なしなら「接続できませんでした」)
- 下に「切断」「Wi-Fi をオフにする / オンにする」「ネットワークの設定…」(`wifi_settings_command`、既定 `nm-connection-editor`、空にすると項目を出さない)
- 開いたときに裏で再スキャンして、4 秒後に一覧を更新する
- `nmcli` が無ければ「nmcli (NetworkManager) がありません」とだけ出る

## Bluetooth メニュー

Bluetooth のアイコンをクリックすると出る (BlueZ を `bluetoothctl` で操作)。

- ペアリング済みの機器の一覧。つながっているものに ✓。選ぶとつなぐ / 切る
- 「Bluetooth をオンにする / オフにする」。オンにするときは rfkill のソフトブロックも外す (`/dev/rfkill` に書く。systemd の機械ではセッションのユーザーが書ける)
- 「Bluetooth の設定…」(`bluetooth_settings_command`、既定 `blueman-manager`、空にすると項目を出さない)。新しい機器のペアリングはこちらに任せる
- 状態 (ブロック・アダプターの起動・接続数) そのものは rfkill・HCI ソケット・`/sys/class/bluetooth` から読む。コマンドは使わない

## 音

音量キーとスピーカーのアイコンで、既定の出力を `wpctl` (PipeWire) で、それが無ければ `pactl` (PulseAudio) で操作する。

- **キー**: `XF86AudioRaiseVolume` / `LowerVolume` で `volume_step` % (既定 5) ずつ。100% より上には上げない。上げるとミュートも解ける。`XF86AudioMute` で出力、`XF86AudioMicMute` でマイクをミュート。結果はアイコンの下に少しの間だけ出る
- アイコンの上で **ホイール** を回すと音量
- **クリック** で音のメニュー: **音量** のつまみ (クリック・ドラッグ、ホイール、選んだ状態で ← →。1 回 `volume_step` %。ミュート中は灰色で、上げるとミュートも解ける)、出力先の一覧 (既定のものに ✓、別のものを選ぶと切り替わる)、「ミュート / ミュートを解除」、「サウンドの設定…」(`sound_settings_command`、既定 `pavucontrol`、空にすると項目を出さない)
- `volume_step 0` でキーを取らない (別のソフトに任せる)

## システムトレイ

バーは freedesktop.org のシステムトレイ (XEmbed) になる。トレイにアイコンを出すアプリは、メモリと Wi-Fi の間にアイコンが並ぶ。アイコンはアプリ自身が描いたもので、クリックもそのアプリに届く。

- GTK 3・Qt・Electron のアプリの多くは、まず StatusNotifierItem の受け手を探し、無ければ XEmbed で出すので、ここに出る
- StatusNotifierItem (D-Bus) にしか対応していないアプリは出ない。[snixembed](https://git.sr.ht/~steef/snixembed) のような橋渡しを入れると XEmbed のトレイに出せる。GTK 4 にはトレイのアイコン自体が無い
- アイコンの大きさはバーの高さに合わせて 16〜24px。隠したいと言ってきたアイコン (`_XEMBED_INFO`) は出さない
- バーが終わるときや読み直すときはアイコンをアプリに返す。アプリは次のトレイに置き直す
- `tray no` でトレイにならない (stalonetray など別のトレイを使うとき)。別のトレイがもう動いていれば、そちらに任せる
- 入力メソッド自身のアイコン (IBus パネルの `ibus-ui-gtk`、fcitx5 の `fcitx`) は、バーがその入力メソッドを出している間は自動で隠す。`input_method none` のときはトレイに出る
- ほかにバー自身が受け持っているもの (nm-applet・blueman-applet のアイコン) は、バーの表示と並んで出る。セッションから外すか、`tray_hide` で隠す
- `tray_hide nm-applet` のように WM_CLASS の名前 (`xprop WM_CLASS` で見える、どちらの半分でも可、空白区切り) を並べると、そのアイコンはトレイに置くが出さない。アプリ自体は動いたまま

## 通知

バーはデスクトップ通知 (`org.freedesktop.Notifications`) を出さない。wm2tj も出さない。通知を出すには、通知デーモンを別に動かす。

- [dunst](https://dunst-project.org/) (`apt install dunst`) が軽い (数 MB)。xfce4-notifyd・mate-notification-daemon・notification-daemon・lxqt-notificationd でもよい
- wm2tj の `wm2-session` は、通知デーモンが動いていなければ、上の順に入っているものを起動する (`WM2TJ_NOTIFYD=<コマンド>` で指定もできる)。別のセッションスクリプトを使うときは、`dunst &` などを足す
- Xubuntu・MATE・KDE などで通知デーモンが入っていれば、D-Bus から自動で起動することも多い。素の Ubuntu (GNOME) は通知を gnome-shell の中で出すので、wm2tj のセッションでは何か入れないと出ない
- 通知デーモンが無いと、`notify-send`・更新の通知・メールやチャットのアプリの通知は何も出ない。Chrome と Firefox は自前の通知に切り替わる

## 入力メソッド

今の入力メソッドを出して、切り替えられる。fcitx5・IBus・XKB (IME なしの複数配列) のどれでも動く。`input_method auto` (既定) は fcitx5 → IBus → XKB の順に動いているものを探す。`fcitx5`・`ibus`・`xkb` で決め打ち、`none` で出さない。どの国の入力メソッドでも、文字はそのエンジン自身のものを出すので、配布するときも既定の `auto` のままでよい。バーが出している間は、その入力メソッド自身のトレイアイコンを隠して 1 つにする (`none` にすると隠さず、トレイのアイコンに任せる)。

入力メソッドは色付き、ただのキーボード配列 (と Mozc の `A` のような直接入力) は枠だけ。

- **fcitx5**: 文字は fcitx5 自身が使うもの: `fcitx5/inputmethod/<名前>.conf` の `Label=` (Mozc なら `あ`)、キーボードなら配列 (`jp`、`us`)。クリックでオン・オフ (`fcitx5-remote -t`。切り替えキーと同じ)
- **IBus**: 文字はエンジンの `<symbol>` (`ibus/component/` の定義ファイル。Mozc `あ`・Pinyin `拼`・Hangul `한`。`<engines exec=…>` でエンジンを出すものは一度だけ聞く)、`xkb:` のエンジンなら配列。クリックで IBus の設定で選んだエンジン (`preload-engines`) の一覧が出て、今のものに ✓。選ぶと `ibus engine <名前>`。最後の項目で `ime_settings_command` (既定 `ibus-setup`) を開く
- **XKB**: IME を使わずに配列を複数使うとき (`setxkbmap -layout us,ru`)、今の配列を出し、メニューに配列の名前 (English (US)、Russian) を並べる。ホットキーで変えてもすぐ反映。配列が 1 つなら出ない

## 電源まわり

xfce4-power-manager などの電源管理ソフトを使わずに済むように、次のことをバーがやる。どれも設定で切れる (項目は [`config.sample`](config.sample))。

- **明るさキー** (`XF86MonBrightnessUp` / `Down`): 1 回で `brightness_step` % (既定 10) ずつ。logind の `SetBrightness` を `busctl` で呼ぶので root は要らない。真っ暗にはならない (下限 1%)
- **省電力モード**: 電源をつないだら `profile_on_ac`、外したら `profile_on_battery` に `powerprofilesctl` で切り替える (power-profiles-daemon)。書かなければ切り替えない
- **画面オフ**: 同じく `screen_off_on_ac` / `screen_off_on_battery` 分で X の DPMS を設定する。書かなければ触らない
- 電源のつなぎ外しは 5 秒ごとに `/sys/class/power_supply` (`Mains` の `online`) で見る
- **バッテリーメニュー** (電池のアイコンをクリック): 残量と充電の状態、**明るさ** のスライダー、**電源モード** (省電力 / バランス / パフォーマンス、今のものに ✓)
  - 明るさ: つまみをクリック・ドラッグ、ホイール、選んだ状態で ← → (1 回 `brightness_step` %、0 のときは 5%)。明るさキーと同じく logind 経由。`busctl` かバックライトが無ければ出ない
  - 電源モード: `powerprofilesctl list` に出るものを並べ、選ぶと `powerprofilesctl set`。power-profiles-daemon が無ければ出ない。選んだモードは、次に電源をつなぎ外しするまで (`profile_on_*` を書いていればそのとき切り替わる)

電源ボタンと蓋は systemd-logind に任せる。電源ボタンの既定はシャットダウンなので、スリープにするなら:

```
# /etc/systemd/logind.conf.d/wm2tj-power.conf
[Login]
HandlePowerKey=suspend
HandleLidSwitch=suspend
HandleLidSwitchExternalPower=suspend
```

反映は `sudo systemctl kill -s HUP systemd-logind`。xfce4-power-manager が動いていると、そちらが電源ボタンと明るさキーを横取りするので止めておく。スリープ前の画面ロックはスクリーンセーバー (xfce4-screensaver など) の役目。電池が切れそうなときの動作は UPower (`/etc/UPower/UPower.conf` の `CriticalPowerAction`)。

## ビルドとインストール

```
sudo apt install build-essential pkg-config libx11-dev libxext-dev libxft-dev gettext   # Debian/Ubuntu
# Fedora: gcc make pkgconf libX11-devel libXext-devel libXft-devel gettext
# Arch:   base-devel libx11 libxext libxft gettext      Alpine: build-base pkgconf libx11-dev libxext-dev libxft-dev gettext
# Void:   base-devel pkg-config libX11-devel libXext-devel libXft-devel gettext
# 設定 GUI (任意): python3 + PyGObject + GTK 3 = python3-gi / python3-gobject / python-gobject / py3-gobject3
# gettext (msgfmt) が無くても make は通る (同梱の .mo をそのまま使う)
make
sudo make install          # /usr/local/bin/wm2tj-bar・wm2tj-bar-settings・その .desktop と翻訳 (/usr/local/share/locale)
```

セッションスクリプト (`wm2-session` など) で wm2tj より前に起動する:

```
wm2tj-bar &
exec wm2tj
```

xfce4-panel と同時に使うと上で重なるので、置き換えるときは xfce4-panel の起動行を外す。

### 依存するもの

ビルドに要るもの: C コンパイラ、pkg-config、Xlib・Xext・Xft の開発用ファイル、gettext (`msgfmt`、翻訳を作る)。ライブラリの同梱はしない。

実行時は X だけあれば動く。次のものは **あれば使う** (無ければその機能が止まるだけで、理由を一度だけ標準エラーに出す):

| コマンド | パッケージ (Debian / Ubuntu) | 使うところ |
|---|---|---|
| `nmcli` | network-manager | Wi-Fi メニュー |
| `nm-connection-editor` | network-manager-gnome | 「ネットワークの設定…」 (`wifi_settings_command` で変えられる) |
| `bluetoothctl`・`timeout` | bluez・coreutils | Bluetooth メニュー |
| `blueman-manager` | blueman | 「Bluetooth の設定…」 (`bluetooth_settings_command` で変えられる) |
| `wpctl` か `pactl` | wireplumber か pulseaudio-utils | 音量キー・音のメニュー |
| `pavucontrol` | pavucontrol | 「サウンドの設定…」 (`sound_settings_command` で変えられる) |
| `fcitx5-remote` | fcitx5 | 入力メソッドの表示 (fcitx5) |
| `ibus`・`gsettings` | ibus・libglib2.0-bin | 入力メソッドの表示とメニュー (IBus) |
| `ibus-setup` | ibus | 「入力メソッドの設定…」 (`ime_settings_command` で変えられる) |
| `busctl` | systemd | 明るさキー (logind の `SetBrightness`) |
| `powerprofilesctl` | power-profiles-daemon | 省電力モードの切り替え (`profile_on_*` を書いたときだけ) |
| `systemctl` / `loginctl` | systemd / elogind | セッションメニューのサスペンド・再起動・シャットダウン (既定。systemctl が無ければ elogind の loginctl) |
| `dbus-send` | dbus | busctl の無い (elogind の) 環境での明るさ変更 |
| `python3`・PyGObject・GTK 3 | python3-gi・gir1.2-gtk-3.0 | 設定 GUI (`wm2tj-bar-settings`)。バー本体は要らない |

### 言語

画面の文字は英語が元で、gettext で翻訳する。いまは日本語 (`po/ja.po`)。`LANG` / `LANGUAGE` が日本語なら日本語、それ以外は英語。アプリ名は .desktop の `Name[ll_CC]` → `Name[ll]` → 言語パックの翻訳 → `Name` の順。

- 文字を足したり変えたりしたら: `make pot` → `msgmerge -U po/ja.po po/wm2tj-bar.pot` → `po/ja.po` を訳す
- 言語を足すときは `po/<言語>.po` を作って、Makefile の `LANGS` に足す

## 設定

`~/.wm2rc` の `tab_width` `tab_length` `tab_gloss` `tab_foreground` `borders` `button_background` `font` を読んで、wm2tj のタブと同じ見た目にする。個別に変えたいときは `~/.config/wm2tj-bar/config` (書式は `~/.wm2rc` と同じ)。項目は [`config.sample`](config.sample)。

- 反映: `pkill -HUP -x wm2tj-bar` (設定を読み直して起動し直す)
- 2 つ目を起動しても「already running」で終わる (画面ごとに 1 つ)

### 設定 GUI

`wm2tj-bar-settings` (■ メニューの「バーの設定…」、アプリの「設定」カテゴリの「トップバーの設定」)。wm2tj とは別の単体のプログラムで、wm2tj の設定 GUI が無くても使える。

- タブ: 見た目 (左端のタブ・バー・時計) / メニュー (wm2tj とメニューを同期する・セッションのコマンド・端末) / 電源 / ネットワークと音 / トレイ
- 各項目の右の「既定」(見た目は「wm2tj と同じ」、電源は「触らない」) にチェックすると、その項目を config から外す (行をコメントにする)。「wm2tj と同じ」は `~/.wm2rc` の値
- 「適用」で、変えた項目の値だけ書き換える (コメント・ほかの行はそのまま、無い項目は末尾に足す)。そのあとバーに HUP を送る。初めて書き換える前に `config.bak-wm2tj-bar-settings` に控える
- 「元に戻す」で、適用していない変更を捨てて読み直す

### セッションコマンドの既定値

| 項目 | 既定 | メモ |
|---|---|---|
| ロック | `xsecurelock` | 要インストール (`apt install xsecurelock`)。i3lock 等でもよい |
| ログアウト | `pkill -TERM -x wm2tj` | wm2tj が終わるとセッションが終わる前提。別のログアウト手順があるならそれを書く |
| サスペンド | `systemctl suspend` | systemctl が無ければ `loginctl suspend` (elogind) |
| 再起動 | `systemctl reboot` | 同 `loginctl reboot` |
| シャットダウン | `systemctl poweroff` | 同 `loginctl poweroff` |
| 端末 (`terminal`) | `$TERMINAL`、無ければ xfce4-terminal → gnome-terminal → konsole → … → xterm | `Terminal=true` のアプリ用。`-e` で開く |

## ウィンドウマネージャー側に要るもの (wm2tj は対応済み)

- `_NET_WM_WINDOW_TYPE_DOCK` のウィンドウに枠を付けない
- `_NET_WM_STRUT_PARTIAL` / `_NET_WM_STRUT` の分だけ、最大化などでバーを避ける
- `_NET_NUMBER_OF_DESKTOPS` / `_NET_CURRENT_DESKTOP` を出し、`_NET_CURRENT_DESKTOP` のクライアントメッセージで切り替える
- `_NET_ACTIVE_WINDOW` を出し、同名のクライアントメッセージで前面に出す (別デスクトップなら切り替え、最小化中なら戻す)
- `_NET_CLIENT_LIST` と各ウィンドウの `_NET_WM_DESKTOP` を出す。`WM_CHANGE_STATE` (IconicState) で最小化し、`WM_STATE` に反映する

## 未決・今後

- メニューのアイコン (v0.1 は無し)
- fcitx5 のメニュー (今はクリックで切り替えるだけ)
- 日付・曜日 (`clock_format` で出せる。既定は HH:MM)

## ライセンス

MIT ([`COPYING`](COPYING))。新規に書いたコードで、wm2 のコードは含まない (タブの光沢の色の計算式は wm2tj で書いたものと同じ)。
