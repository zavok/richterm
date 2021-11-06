</$objtype/mkfile

BIN=/$objtype/bin
TARG=richterm
OFILES=richterm.$O fs.$O array.$O
HFILES=richterm.h array.h

</sys/src/cmd/mkone

acid:V:
	$CC -a *.c > acid.lib

DIRS=extra

all:V:	$DIRS

$DIRS:V:
	for (i in $DIRS) @{
		echo mk $i
		cd $i
		mk all
	}

install:V: installdirs

nuke:V: nukedirs

installdirs:V:
		for (i in $DIRS) @{
			echo mk $i
			cd $i
			mk install
		}

clean:V:
		for (i in $DIRS) @{
			echo clean $i
			cd $i
			mk clean
		}
		rm -f [$OS].* *.[$OS]

nukedirs:V:
	for (i in $DIRS) @{
		echo nuke $i
		cd $i
		mk nuke
	}

update:V:
	update $UPDATEFLAGS $UPDATE
	for (i in $DIRS) @{
		echo update $i
		cd $i
		mk 'UPDATEFLAGS='$"UPDATEFLAGS update
	}
