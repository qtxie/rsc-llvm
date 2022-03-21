Red/System []

#include %../src/runtime/common.reds

print-cmdline-args: func [
	/local
		args	[str-array!]
		src		[c-string!]
][
	probe system/args-count

	args: system/args-list
	until [
		src: args/item
		probe src
		args: args + 1
		null? args/item
	]
]

print-cmdline-args

quit 0