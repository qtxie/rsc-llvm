Red/System []

fib: func [n [integer!] return: [integer!]][
    if n <= 1 [return n]
    (fib n - 1) + (fib n - 2)
]

a: 10
fib a