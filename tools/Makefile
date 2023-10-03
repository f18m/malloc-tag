EXAMPLE_JSON_FILE=../examples/multithread/multithread_stats.json

python_tests:
	@echo "Starting Python integration TESTS (assume multithread example JSON is available)"
	$(MAKE) -s test_load_and_save_without_processing
	$(MAKE) -s test_thread_aggregation

test_load_and_save_without_processing:
	@echo
	@echo ">> test_load_and_save_without_processing <<"
	@echo
	./postprocess.py -o /tmp/nopostprocess.json $(EXAMPLE_JSON_FILE)
	jq . /tmp/nopostprocess.json >/tmp/nopostprocess_prettyprinted.json
	jq . $(EXAMPLE_JSON_FILE) >/tmp/input_prettyprinted.json
	md5sum /tmp/input_prettyprinted.json /tmp/nopostprocess_prettyprinted.json
	@cmp --silent /tmp/input_prettyprinted.json /tmp/nopostprocess_prettyprinted.json || ( \
		echo; \
		echo "!! Failed test; the two files are different !!" ; \
		diff -bU3 /tmp/input_prettyprinted.json /tmp/nopostprocess_prettyprinted.json ; \
		echo; \
		exit 2 \
	)
	rm -f /tmp/nopostprocess_prettyprinted.json /tmp/input_prettyprinted.json

test_thread_aggregation:
	@echo
	@echo ">> test_thread_aggregation <<"
	@echo
	./postprocess.py -o /tmp/threadagg.json -c example_agg_rules.json $(EXAMPLE_JSON_FILE)
	jq . /tmp/threadagg.json >/tmp/nopostprocess_prettyprinted.json
	jq . $(EXAMPLE_JSON_FILE) >/tmp/input_prettyprinted.json
