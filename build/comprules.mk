##-- comma/build/comprules.mk --------------------------------*- Makefile -*--##
#
# This file is distributed under the MIT License.  See LICENSE.txt for details.
#
# Copyright (C) 2008, Stephen Wilson
#
##----------------------------------------------------------------------------##
#
# This file contains the rules for compiling source files into object files.
#
##----------------------------------------------------------------------------##

#
# C++ source files.
#
prj_cxx_sources = $(notdir $(wildcard $(prj_srcdir)/*.cpp))

#
# C++ object files.
#
prj_cxx_objects = $(addsuffix .o, $(basename $(prj_cxx_sources)))

#
# C source files.
#
prj_c_sources = $(notdir $(wildcard $(prj_srcdir)/*.c))

#
# C object files.
#
prj_c_objects = $(addsuffix .o, $(basename $(prj_c_sources)))

#
# All object files.
#
prj_objects  = $(addprefix $(prj_objdir)/,$(prj_cxx_objects))
prj_objects += $(addprefix $(prj_objdir)/,$(prj_c_objects))

#
# Dependency files.
#
prj_depends = $(patsubst %.o,%.d,$(prj_objects))

-include $(prj_depends)

#
# Generic C++ build rule.
#
$(prj_objdir)/%.o: $(prj_srcdir)/%.cpp
	$(verb) set -e;                                           \
	echo "compiling $(notdir $<)";                            \
	$(CXX) -MM $(cpp_flags) -o $*.d.tmp $<;                   \
	$(SED) -e 's|.*:|$(prj_objdir)/$*.o:|' < $*.d.tmp > $*.d; \
	$(RM)  -f $*.d.tmp;                                       \
	$(CXX) $(cpp_flags) $(cxx_flags) -c -Wall $< -o $@

#
# Generic C build rule.
#
$(prj_objdir)/%.o: $(prj_srcdir)/%.c
	$(verb) set -e;                                           \
	echo "compiling $(notdir $<)";                            \
	$(CXX) -MM $(cpp_flags) -o $*.d.tmp $<;                   \
	$(SED) -e 's|.*:|$(prj_objdir)/$*.o:|' < $*.d.tmp > $*.d; \
	$(RM)  -f $*.d.tmp;                                       \
	$(CC) $(cpp_flags) $(c_flags) -c -Wall $< -o $@

#
# Extend the clean target to handle the generated object and dependency files.
#
.PHONY: clean-compilation

clean: clean-compilation

clean-compilation:
	$(verb) set -e; \
	$(RM) -f $(prj_objects) $(prj_depends)
