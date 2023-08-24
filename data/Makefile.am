uidir = $(datadir)/rp-prefapps

ui_in_files = 	rp_prefapps.ui \
				prefapps.conf \
				bluej.png \
				claws-mail.png \
				com.visualstudio.code.png \
				ctc.png \
				greenfoot.png \
				kicad.png \
				libreoffice-main.png \
				mage.png \
				mu.png \
				orca.png \
				scratch.png \
				scratch3.png \
				sense_emu_gui.png \
				sonic-pi.png \
				smartsim.png \
				thonny.png \
				vncviewer48x48.png \
				wolfram-mathematica.png \
				chromium-browser.png \
				firefox.png
	
ui_DATA = $(ui_in_files)

desktopdir=$(datadir)/applications

desktop_in_files= \
	rp-prefapps.desktop.in \
	$(NULL)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = $(ui_in_files) \
			$(desktop_in_files) \
			$(desktop_DATA) \
			$(NULL)
