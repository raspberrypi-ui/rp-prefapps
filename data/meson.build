data = files (
    'rp_prefapps.ui',
    'prefapps.conf',
    'claws-mail.png',
    'com.visualstudio.code.png',
    'ctc.png',
    'kicad.png',
    'libreoffice-main.png',
    'mage.png',
    'mu.png',
    'orca.png',
    'rpc.png',
    'scratch.png',
    'scratch3.png',
    'sense_emu_gui.png',
    'thonny.png',
    'vncviewer48x48.png',
    'wolfram-mathematica.png',
    'chromium-browser.png',
    'firefox.png',
    'squeekboard.png'
)

install_data(data, install_dir: resource_dir)
install_data('10-rp-prefapps.rules', install_dir: polkit_dir)

i18n.merge_file(input: 'rp-prefapps.desktop.in',
      output: 'rp-prefapps.desktop',
      type: 'desktop',
      po_dir: '../po',
      install: true,
      install_dir: desktop_dir
)
