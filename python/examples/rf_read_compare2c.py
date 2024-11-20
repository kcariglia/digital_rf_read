"""
Python version of c_read examples to 
ensure same output

K Cariglia, 11/5/24
"""

import digital_rf

dir1 = "/Users/cariglia/example_digital_rf"
dir2 = "/Users/cariglia/Desktop/drfexamples/hprec_channels"
dir3 = "/Users/cariglia/Desktop/drfexamples/hprec_subchannels"


print("Test 1: basic example")
print("----------------------")
dro1 = digital_rf.DigitalRFReader(dir1)
channels1 = dro1.get_channels()
print("got channels: {}".format(channels1))
s1, e1 = dro1.get_bounds(channels1[0])
print("got bounds: {} {}".format(s1, e1))
cont_data_arr = dro1.get_continuous_blocks(s1, e1, channels1[0])
print("got data arr:")
print(cont_data_arr)
print()


print("Test 2: real data, no subchannels")
print("----------------------")
dro2 = digital_rf.DigitalRFReader(dir2)
channels2 = dro2.get_channels()
print("got channels: {}".format(channels2))
s2, e2 = dro2.get_bounds(channels2[0])
print("got bounds: {} {}".format(s2, e2))
cont_data_arr = dro2.get_continuous_blocks(s2, e2, channels2[0])
print("got data arr:")
print(cont_data_arr)
print()


print("Test 3: real data, yes subchannels")
print("----------------------")
dro3 = digital_rf.DigitalRFReader(dir3)
channels3 = dro3.get_channels()
print("got channels: {}".format(channels3))
s3, e3 = dro3.get_bounds(channels3[0])
print("got bounds: {} {}".format(s3, e3))
cont_data_arr = dro3.get_continuous_blocks(s3, e3, channels3[0])
print("got data arr:")
print(cont_data_arr)
print()

