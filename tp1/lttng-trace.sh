lttng create inf8601_trace
lttng enable-event -ak
lttng start
./trace-dragon
lttng stop
traceViewer
