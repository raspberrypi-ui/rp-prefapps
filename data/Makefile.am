uidir = $(datadir)/rp_prefapps

ui_in_files = 	rp_prefapps.ui \
				prefapps.conf \
				bluej.png \
				claws-mail.png \
				greenfoot.png \
				mu.png \
				node-red-icon.svg \
				sonic-pi.png \
				vncviewer48x48.png \
				wolfram-mathematica.png
	
ui_DATA = $(ui_in_files)

desktopdir=$(datadir)/applications

desktop_in_files= \
	rp_prefapps.desktop.in \
	$(NULL)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = $(ui_in_files) \
			$(desktop_in_files) \
			$(desktop_DATA) \
			$(NULL)
