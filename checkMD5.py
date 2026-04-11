import subprocess

expect = '23836c35e58bd31c34f6b7edb26b5fe2'

output = subprocess.check_output(args = 'md5sum olympics-cmudb2024.db.gz',shell=True).decode().split(' ')[0]

if output == expect:
    print("Same!")
else:
    print("Different!")



