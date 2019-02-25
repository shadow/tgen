# JSON DB Structure for Analysis Results

The `tgentools` python toolkit can be run in `parse` mode to parse a set of
tgen log files. It writes useful statistics to a json file.

This document describes the structure of the json database file that gets exported
when running the `tgentools` python toolkit in `parse` mode.

The structure is given here with variable keys marked as such.

    {
      "data": { # generic keyword
        "phantomtrain": { # nickname of the OnionPerf client, hostname if not set
          "measurement_ip" : "192.168.1.1", # public-facing IP address of the machine used for the measurements
          "tgen": { # to indicate data from TGen
            "transfers": { # the records for transfers TGen attempted
              "transfer1m:1": { # the id of a single transfer
                "elapsed_seconds": { # timing for various steps in transfer, in seconds
                  "checksum": 0.0, # step 12 if using a proxy, else step 8 (initial GET/PUT)
                  "command": 0.319006, # step 7 if using a proxy, else step 3 (initial GET/PUT)
                  "first_byte": 0.0, # step 9 if using a proxy, else step 5 (initial GET/PUT)
                  "last_byte": 0.0, # step 11 if using a proxy, else step 7 (initial GET/PUT)
                  "payload_progress": { # step 10 if using a proxy, else step 6 (initial GET/PUT)
                    "0.0": 0.0, # percent of payload completed : seconds to complete it
                    "0.1": 0.0,
                    "0.2": 0.0,
                    "0.3": 0.0,
                    "0.4": 0.0,
                    "0.5": 0.0,
                    "0.6": 0.0,
                    "0.7": 0.0,
                    "0.8": 0.0,
                    "0.9": 0.0,
                    "1.0": 0.0
                  },
                  "proxy_choice": 0.000233, # step 4 if using a proxy, else absent
                  "proxy_init": 0.000151, # step 3 if using a proxy, else absent
                  "proxy_request": 0.010959, # step 5 if using a proxy, else absent
                  "proxy_response": 0.318873, # step 6 if using a proxy, else absent
                  "response": 0.0, # step 8 if using a proxy, else step 4 (initial GET/PUT)
                  "socket_connect": 0.000115, # step 2
                  "socket_create": 2e-06 # step 1
                },
                "endpoint_local": "localhost:127.0.0.1:45416", # tgen client socket name:ip:port
                "endpoint_proxy": "localhost:127.0.0.1:27942", # proxy socket name:ip:port, if present
                "endpoint_remote": "server1.peach-hosting.com:216.17.99.183:6666", # tgen server hostname:ip:port
                "error_code": "READ", # 'NONE' or a code to indicate the type of error
                "filesize_bytes": 1048576, # size of the transfer payload
                "hostname_local": "puntaburros.amerinoc.com", # client machine hostname
                "hostname_remote": "(null)", # server machine hostname
                "is_commander": true, # true if client (initiated the transfer), else false
                "is_complete": true, # if the transfer finished, no matter the error state
                "is_error": true, # if there was an error in the transfer
                "is_success": false, # if the transfer completed and checksum passed
                "method": "GET", # transfer method (GET,PUT)
                "payload_bytes_status": 0, # cumulative number of payload bytes received
                "total_bytes_read": 0, # total bytes read from the socket
                "total_bytes_write": 50, # total written to the socket
                "transfer_id": "transfer1m:1", # the id of this transfer, unique to this run of OnionPerf
                "unix_ts_end": 1456699868.006196, # initial start time of the transfer
                "unix_ts_start": 1456699868.006196 # final end time of the transfer
              },
            },
            "transfers_summary": { # summary stats of all transfers in the 'transfers' section
              "errors": {
                "PROXY": { # PROXY type errors
                  "1456654221": [ # the second at which the error occurred
                    51200 # transfer filesizes that had errors, one entry for each error during this second
                  ],
                },
                "READ": { # READ type errors
                  "1456618782": [ # second at which the error occurred
                    51200 # transfer filesize, one for each error at this time
                  ],
                },
              "time_to_first_byte": { # time to receive the first byte of the payload
                "51200": { # file size
                  "1456707932": [ # the second at which the transfer completed
                    0.36213199999999995 # time to first byte, in seconds
                  ],
                },
              },
              "time_to_last_byte": { # time to receive the last byte of the payload
                "51200": { # file size
                  "1456707932": [ # the second at which the transfer completed
                    0.6602399999999999 # time to last byte, in seconds
                  ],
                }
              }
            },
          },
        }
      }
    }
