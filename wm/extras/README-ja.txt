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

操作
  タブ左ドラッグ=移動 / タブWクリック=最大化 / タブ中クリック=最小化
  タブの四角=閉じる / 枠の右下=リサイズ
  背景の左クリック=窓の切替 / 背景の右クリック=メニュー
  別のデスクトップへ移す=パネルのタスクリストでボタンを右クリック→「別のワークスペースへ移動」

ビルドと導入
  sudo apt install g++ make libx11-dev libxext-dev libxmu-dev libxft-dev libxi-dev libfontconfig-dev picom
  make && sudo install -m755 wm2tj /usr/local/bin/wm2tj
  sudo install -m755 wm2-session /usr/local/bin/wm2-session      # fcitx5/picom/xfce4-panel を起動して wm2tj を exec
  sudo make install-session   # wm2-session と wm2tj.desktop（上の階層の Makefile）
  ※ wm2-session は XDG_CONFIG_DIRS/XDG_DATA_DIRS を Xubuntu と同じにする（パネルのメニューを Xfce と揃える）。また polkit エージェント・壁紙（wm2-wallpaper）も起動し、
    ~/.wm2rc が無ければ wm2-menu-gen で作る。nm-applet・blueman-applet・xfce4-power-manager は wm2tj-bar が無いときだけ（バーと役割が重なるため）
  ※ .desktop の Exec に sh -c '...' を書くと Xsession が引用符を無視して起動しない。起動処理は wm2-session に書く。

docs/
  開発記録.md               改造の経緯・原因・テスト方法（作業記録からの抜粋）

extras/
  wm2rc.sample              -> $(PREFIX)/share/wm2tj/  ~/.wm2rc が無いとき wm2-menu-gen がこれを設定部分にして作る
                               （手で ~/.wm2rc にコピーしてもよい。メニューは wm2-menu-gen で追記）
  wm2-menu-gen              -> ~/.local/bin/  右クリックメニュー（お気に入り→wm2tj→カテゴリ）を ~/.wm2rc に生成
                               お気に入りは ~/.config/wm2tj/favorites。無ければ wm2tj-bar のお気に入り
                               （~/.config/wm2tj-bar/favorites）→ Whisker Menu のお気に入り の順に写して作る
                               お気に入りの後に submenu "wm2tj"（設定…・お気に入りの編集…・壁紙を変更）を出す
                               壁紙の項目は wm2-wallpaper があるときだけ
                               末尾の「ログアウト」「再起動」「シャットダウン」は session-logout を呼ぶ
                               （PATH に無ければ ~/.local/bin/session-logout）
  wm2tj-settings            -> ~/.local/bin/  wm2tj の設定 GUI（GTK3/Python、.wm2rc の見た目を変えて HUP）。メニュー wm2tj → 設定…
  wm2tj-favorites           -> ~/.local/bin/  お気に入りの編集 GUI（~/.config/wm2tj/favorites）。メニュー wm2tj → お気に入りの編集…
                               ファイルが無い間は wm2-menu-gen と同じ順（wm2tj-bar → Whisker Menu）で一覧を出し、最初の変更で作る
                               変えるたびに wm2-menu-gen を走らせて wm2tj に HUP（右クリックメニューがすぐ追従）
                               一番上に wm2tj 組み込みの「ターミナル」（~/.wm2rc の new_label / new_window）も出る。
                               別の端末を使うなら削除できる（new_label none にする。名前はコメントに残す）。戻すには右の一覧の先頭から追加
  wm2-menu-sync.path/.service -> ~/.local/share/systemd/user/  任意。アプリの追加・削除を右クリックメニューに反映する
                               systemd user の path ユニット。systemctl --user daemon-reload &&
                               systemctl --user enable --now wm2-menu-sync.path で有効化。systemd の無い環境では使わない
                               （お気に入りの変更は wm2tj-favorites と wm2tj-bar が自分で反映するので、これは無くてもよい）
  wm2-wallpaper             -> ~/.local/bin/  壁紙（~/.config/wm2tj/wallpaper に保存。GNOME のスキーマがあればそちらにも書く。--choose で選ぶ）。メニュー wm2tj → 壁紙を変更
                               貼るのは feh / xwallpaper / hsetroot のどれか、選ぶのは zenity / yad / PyGObject の GTK ダイアログのどれか
  Makefile                  上の 4 本・session-logout・power-dialog・翻訳・systemd ユニットを入れる。make install（/usr/local）か make install PREFIX=$HOME/.local
                               画面の文字は英語が元で gettext（text domain "wm2tj"）。訳は po/ja.po
                               文字を変えたら make pot → msgmerge -U po/ja.po po/wm2tj.pot → 訳を足して make
  session-logout            -> ~/.local/bin/  Xfce なら xfce4-session-logout、wm2tj なら wm2tj を終了。
                               --halt/--reboot/--suspend/--hibernate は systemctl へ。Whisker の電源ボタンもこれに向ける:
                               xfconf の /plugins/plugin-<id>/command-shutdown 等（パネル停止中に書く）
                               --ask を付けると power-dialog で確認してから実行（wm2tj のメニューとパネルの電源ボタンが使う）。
                               power-dialog は同じ場所か PATH から探し、起動できなければ zenity で確認
  wm2-swipe.c               -> ~/.local/bin/wm2-swipe  3 本指の横スワイプで仮想デスクトップ移動（C、XInput 2.4 のジェスチャーを
                               root でパッシブグラブ。root 権限も input グループも不要。右=次・左=前、-r で逆、-w で端から折り返し、
                               -f 指の数、-t しきい値。libxi-dev があるときだけビルド。wm2-session が起動する）
  power-dialog              -> ~/.local/bin/  電源の確認画面（GTK3/Python。画面を暗くして中央にカード。
                               シャットダウン・再起動・サスペンド・ログアウトを選べる。選んだオプションを出力）
  panel-power/*.desktop     -> ~/.config/xfce4/panel/launcher-<id>/  パネル右端の電源ボタン（ランチャー）の項目。
                               items に 01〜04 の順で登録、arrow-position 0（5 だと矢印がアイコンに重なる）。
                               アイコン＝シャットダウン、▾ で再起動・サスペンド・ログアウト
  wm2_badge-symbolic.svg    -> /usr/share/icons/hicolor/scalable/places/ (ログイン画面のバッジ)
  lightdm-greeter-gtk.css   -> /var/lib/lightdm/.config/gtk-3.0/gtk.css (セッション選択ボタンを黄色に)
  lightdm-sessions/*.desktop -> /usr/share/lightdm/sessions/ (ログイン画面のセッション名を日本語化・重複を隠す)
  autostart-picom-disable.desktop -> ~/.config/autostart/picom.desktop (Xfce セッションで picom を起動させない)
