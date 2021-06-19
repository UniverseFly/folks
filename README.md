# folks: a 'forked' file system

> This project is forked and then developed based on [cse-30341-fa17-project06](https://gitlab.com/nd-cse-30341-fa17/cse-30341-fa17-project06). For more information, look into https://www3.nd.edu/~pbui/teaching/cse.30341.fa17/project06.html.

## Build and test (for Linux and macOS only)

Simply clone the project and do `make` and `make test`.

## Play with the shell interface

```shell
$ ./bin/folks ./data/yuxiang.1000 1000
folks> mount 
disk mounted.
folks> copyout 0 yuxiang.jpg
2042182 bytes copied
folks> quit
1595 disk block reads
0 disk block writes
```

Now `yuxiang.jpg` is copied out from `folks` to your local file system. For more commands, type `help`:

```shell
folks> help
Commands are:
    format
    mount
    debug
    create
    remove  <inode>
    cat     <inode>
    stat    <inode>
    copyin  <file> <inode>
    copyout <inode> <file>
    help
    quit
    exit
```

## Acknowledgement

These two repositories help me a lot during implementation:

- https://gitlab.com/bosompaddy/cse-30341-fa17-project06
- https://gitlab.com/dodunayo/cse-30341-fa18-project06
