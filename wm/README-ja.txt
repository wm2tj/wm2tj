wm2tj
wm2 をもとにした改造版のウィンドウマネージャー。2026-10 から数台のノート PC (Xubuntu 系) で日常使用しながら改造している。

変更点
- 移動/リサイズ時の 50ms ポーリングを廃止、-O2 ビルド
- xfce4-panel 対応（DOCK 窓は枠なし最前面、タスクリスト/ページャー用 EWMH、strut）
- タブ文字を Xft + UTF-8（日本語タイトル）
- タブ長固定 tab_length（既定 300px、0 で伸縮）
- Whisker Menu 対応（TYPE_MENU を枠なし・フォーカス付きで表示）
- `pkill -HUP -x wm2tj` で同じ PID のまま再起動（設定再読込、窓のデスクトップ維持）。ルートメニューにも項目あり
- タブの光沢グラデーション tab_gloss、ARGB 窓の透過（picom）
- 最大化: タブのダブルクリックで最大化⇔復元（間隔 double_click_time、既定 1300ms）。
  アプリからの _NET_WM_STATE（最大化・全画面）にも従う。全画面はパネルより前面
- 最小化: タブの中クリック（タッチパッドは3本指タップ）。戻すのはタスクリストか、背景の右クリックで出るメニュー
- タッチパッドのダブルタップが背景クリックに化けてカーソルが飛ぶ問題を修正
- 窓の中をクリックすると前面に出る（フォーカスはポインタ追従のまま。ホイールでは出ない）
- 既定の見た目＝青い光沢タブ＋白い細枠（tab_gloss none で元の灰色）。タブの太さ tab_width（既定32px）
- 別デスクトップの窓へフォーカスを戻そうとしてキーボードが効かなくなる不具合を修正
- Whisker Menu を閉じた後にフォーカスが外れ、IME（fcitx5）が切り替わらなくなる不具合を修正
- ~/.wm2rc の new_window（ルートメニューの「ターミナル」）に引数を書けるように（行の残り全部を /bin/sh で実行）。
  例: new_window tilix --new-process --session="$HOME/.config/tilix/sessions/two-pane.json"
  favseed 版までは最初の 1 語しか読まず、2 画面の指定などが捨てられて素の端末が開いていた
- new_window の既定を空に: 書かなければ（またはそのコマンドが無ければ）$TERMINAL → x-terminal-emulator（Debian/Ubuntu 系の既定の端末）
  → xfce4-terminal → gnome-terminal → … → xterm の順で、その人の端末を開く。項目名の既定は「Terminal」（以前は xterm と「New」）。
  wm2rc.sample も new_window をコメントにし、new_label "Terminal"（日本語の例はコメント）
- お気に入りの編集（wm2tj-favorites）の一番上に組み込みの「ターミナル」を表示。別の端末を使うなら削除できる（new_label none）。
  右の一覧の先頭から戻せる
- HUP（再起動）の終了処理で、アクティブな窓のフォーカスの戻り先が解放済みの窓を指したまま activate され、
  segfault してセッションごと落ちることがあった不具合を修正

操作
  タブ左ドラッグ=移動 / タブWクリック=最大化 / タブ中クリック=最小化
  タブの四角=閉じる / 枠の右下=リサイズ
  背景の左クリック=窓の切替 / 背景の右クリック=メニュー
  別のデスクトップへ移す=パネルのタスクリストでボタンを右クリック→「別のワークスペースへ移動」

ビルドと導入
  sudo apt install g++ make pkg-config libx11-dev libxext-dev libxft-dev libxrandr-dev libxi-dev picom   # Debian/Ubuntu（libxi-dev は extras の wm2-swipe 用、無ければ wm2-swipe だけ省かれる）
    Fedora: gcc-c++ make pkgconf libX11-devel libXext-devel libXft-devel libXrandr-devel picom
    Arch: base-devel libx11 libxext libxft libxrandr libxi picom   Alpine: build-base pkgconf libx11-dev libxext-dev libxft-dev libxrandr-dev libxi-dev picom
    （ヘッダとライブラリは pkg-config で探す。g++ でも clang++ でもビルドできる。CXX/CXXFLAGS/LDFLAGS/DESTDIR/PREFIX に従う）
  make && sudo make install              # /usr/local/bin/wm2tj
  sudo make install-session              # wm2-session → /usr/local/bin、wm2tj.desktop → /usr/share/xsessions
  ※ wm2-session は入っているものだけを起動する: fcitx5（無ければ ibus。ibus は im-config 等が先に起動していれば
    二重に起動しない）・wm2-swipe（無ければ libinput-gestures）・picom・wm2-wallpaper（壁紙）・xfsettingsd（無ければ xsettingsd）・
    polkit エージェント（mate/gnome/xfce/lxpolkit/kde のどれか。WM2TJ_POLKIT_AGENT= で指定可）・通知デーモン・
    wm2tj-bar。wm2tj-bar が無いときだけ xfce4-panel・nm-applet・blueman-applet・xfce4-power-manager
    （バーが Wi-Fi/Bluetooth/輝度キー/電源プロファイルを持つので、あるとトレイが二重になり電源キーを取り合う）。
    ~/.wm2rc が無ければ（初回ログイン）先に wm2-menu-gen を走らせて wm2rc.sample の設定 + メニューを作る。
    XDG_DATA_DIRS/XDG_CONFIG_DIRS はログインマネージャーの値を残し、Xfce/Xubuntu・Flatpak・snap のディレクトリがあれば足す。
    XDG_CURRENT_DESKTOP=wm2tj:XFCE（Xfce の設定ツールをメニューに出すため）
  ※ .desktop の Exec に sh -c '...' を書くと Xsession が引用符を無視して起動しない。起動処理は wm2-session に書く。
  ※ 通知（org.freedesktop.Notifications）は wm2tj も wm2tj-bar も出さない。通知デーモンが要る。
    wm2-session は、通知デーモンが動いていなければ dunst → xfce4-notifyd → mate-notification-daemon →
    notification-daemon → lxqt-notificationd の順に、入っているものを起動する（WM2TJ_NOTIFYD=コマンド で指定も可）。
    どれも無ければ通知は出ない。素の Ubuntu（GNOME）は通知を gnome-shell が出すので、wm2tj では何か入れる:
      sudo apt install dunst      # 軽い（数 MB）。見た目は ~/.config/dunst/dunstrc
    Chrome・Firefox は通知デーモンが無いと自前の通知に切り替わるが、notify-send・更新通知・メール等は何も出ない。

docs/
  開発記録.md               改造の経緯・原因・テスト方法（作業記録からの抜粋）

extras/
  wm2rc.sample              -> $(PREFIX)/share/wm2tj/  ~/.wm2rc が無いとき wm2-menu-gen がこれを設定部分にして作る
                               （手で ~/.wm2rc にコピーしてもよい。メニューは wm2-menu-gen で追記）
  wm2-menu-gen              -> ~/.local/bin/  Whisker Menu と同じメニュー（お気に入り→カテゴリ）を ~/.wm2rc に生成
                               末尾の「ログアウト」等は session-logout（PATH、無ければ ~/.local/bin）を呼ぶ
  session-logout            -> ~/.local/bin/  Xfce なら xfce4-session-logout、wm2tj なら wm2tj を終了。
                               --halt/--reboot/--suspend/--hibernate は systemctl へ。Whisker の電源ボタンもこれに向ける:
                               xfconf の /plugins/plugin-<id>/command-shutdown 等（パネル停止中に書く）
  wm2_badge-symbolic.svg    -> /usr/share/icons/hicolor/scalable/places/ (ログイン画面のバッジ)
  lightdm-greeter-gtk.css   -> /var/lib/lightdm/.config/gtk-3.0/gtk.css (セッション選択ボタンを黄色に)
  lightdm-sessions/*.desktop -> /usr/share/lightdm/sessions/ (ログイン画面のセッション名を日本語化・重複を隠す)
  autostart-picom-disable.desktop -> ~/.config/autostart/picom.desktop (Xfce セッションで picom を起動させない)
