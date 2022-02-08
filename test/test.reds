Red/System []

fib: func [n [integer!] return: [integer!] /local a b [integer!]][
   if n <= 1 [ return n]
   (fib n - 1) + (fib n - 2)
]

printf ["%d" fib 10]
