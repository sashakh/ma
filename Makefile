# preliminary makefile

sub-dirs:= m

all:

all clean dep: %: %-sub-dirs

%-sub-dirs:
	$(foreach d, $(sub-dirs), $(MAKE) -C $(d) $* && ) echo "$@ done."

clean: clean-final

clean-final:
	find -type f -name '*.[ao]' -o -name '*~' -o -name '.depend' -o -name '*.out' | xargs $(RM)
