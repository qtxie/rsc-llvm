Red/System []

foo: func [n [integer!]][
	if n < 2 [
      throw 10
   ]
]

foo1: func [][
   catch 11 [ 
      printf "a"
      foo 1
      printf "b"
   ]
   printf "c"
]

foo1