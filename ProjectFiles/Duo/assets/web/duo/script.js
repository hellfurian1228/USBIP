$(document).ready(function() {
	var pollInterval; // Variable to hold the interval ID

	// Disable all buttons initially
	$(".button").prop("disabled", true);
	$(".label").css("pointer-events", "none");

	// Start polling instance status
	startPolling();

	// Button click event handler
	$(".button").click(function() {
		var instance = $(this).data("instance");
		var action = $(this).data("action");
		$(".button").prop("disabled", true);
		clearInterval(pollInterval); // Pause polling
		executeInstanceAction(instance, action);
		if (instance === "") {
			executeServiceAction(action);
		} else {
			executeInstanceAction(instance, action);
		}
	});

	// Function to start polling instance status
	function startPolling() {
		pollInterval = setInterval(function() {
			$(".row").each(function() {
				var instanceName = $(this).find(".label").text().trim();
				var url = "/instances/" + instanceName;

				// Make GET request to get instance status
				$.get(url, function(data) {
					var running = data.trim() === "true";
					updateButtons(instanceName, running);
				});
			});
		}, 5000);
	}

	// Function to update buttons based on instance status
	function updateButtons(instance, running) {
		// Re-enable the service restart button
		$(".button-service-restart").prop("disabled", false);

		// Update the instance rows
		$(".row").each(function() {
			var instanceName = $(this).find(".label").text().trim();
			if (instanceName === instance) {
				$(this).find(".button-instance-start").prop("disabled", running);
				$(this).find(".button-instance-stop").prop("disabled", !running);
				if (running) {
					$(this).find(".label").css("pointer-events", "auto");
				} else {
					$(this).find(".label").css("pointer-events", "none");
				}
			}
		});
	}

	// Function to execute service action
	function executeServiceAction(action) {
		var url = "/" + action;
		$.get(url, function(data) {
			showMessage(data);
		});
	}

	// Function to execute instance action
	function executeInstanceAction(instance, action) {
		var url = "/instances/" + instance + "/" + action;
		$.get(url, function(data) {
			showMessage(data);
		});
	}

	// Function to show message
	function showMessage(message) {
		$("#message").text(message);
		$("#message").fadeIn('slow');
		sleep(5000).then(() => {
			$("#message").fadeOut('slow', function() {
				startPolling(); // Restart polling
			});
		});
	}

	// Function to sleep
	function sleep(time) {
		return new Promise((resolve) => setTimeout(resolve, time));
	}
});