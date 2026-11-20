package main

import "fmt"

func main() {
    total := 0
    i := 1
    for i <= 5 {
        total += i
        i += 1
    }
    fmt.Println("sum=", total)
}
