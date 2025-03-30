import os
import subprocess
from datetime import datetime
import re
import multiprocessing
from concurrent.futures import ProcessPoolExecutor, as_completed

def strip_ansi(text):
    """Remove ANSI escape sequences"""
    ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    return ansi_escape.sub('', text)

def parse_sla_line(line):
    """Parse SLA line and return dict with compliance info"""
    # Strip ANSI escape sequences first
    clean_line = strip_ansi(line)
    match = re.match(r"SLA(\d+): Violations = (\d+)/(\d+) \(([\d.]+)% violations, ([\d.]+)% compliance\) \[Required: ([\d.]+)% compliance\]", clean_line)
    if match:
        sla_id = int(match.group(1))
        violations = int(match.group(2))
        total = int(match.group(3))
        compliance = float(match.group(5))
        required = float(match.group(6))
        passed = compliance >= required
        return {
            'sla_id': sla_id,
            'violations': violations,
            'total': total,
            'compliance': compliance,
            'required': required,
            'passed': passed
        }
    return None

def generate_html_report(results, filename):
    """Generate HTML report with colored results and summary table"""
    html_template = """
    <!DOCTYPE html>
    <html>
    <head>
        <title>CloudSim Test Results - {timestamp}</title>
        <style>
            body {{ 
                font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif; 
                margin: 40px auto;
                max-width: 1200px;
                padding: 0 20px;
                line-height: 1.6;
            }}
            .test-case {{ 
                margin-bottom: 30px;
                border: 1px solid #e1e4e8;
                border-radius: 6px;
                padding: 20px;
                background: #fff;
                box-shadow: 0 1px 3px rgba(0,0,0,0.1);
            }}
            .test-header {{
                border-bottom: 1px solid #e1e4e8;
                margin-bottom: 15px;
                padding-bottom: 10px;
            }}
            .pass {{ color: #28a745; }}
            .fail {{ color: #dc3545; }}
            .summary-table {{
                width: 100%;
                border-collapse: collapse;
                margin: 20px 0 30px 0;
                font-size: 0.9em;
                box-shadow: 0 1px 3px rgba(0,0,0,0.1);
            }}
            .summary-table th, .summary-table td {{
                padding: 12px;
                text-align: left;
                border: 1px solid #e1e4e8;
            }}
            .summary-table th {{
                background: #f6f8fa;
                font-weight: 600;
            }}
            .summary-table tr:hover {{
                background-color: #f8f9fa;
            }}
            .compliance-cell {{
                text-align: center;
                padding: 4px 8px;
                border-radius: 4px;
                width: 80px;
                display: inline-block;
            }}
            .compliance-high {{ background-color: #d4edda; }}
            .compliance-med {{ background-color: #fff3cd; }}
            .compliance-low {{ background-color: #f8d7da; }}
            .sla-table {{ /* ... existing sla-table styles ... */ }}
            .metrics {{ /* ... existing metrics styles ... */ }}
            .summary {{ /* ... existing summary styles ... */ }}
        </style>
    </head>
    <body>
        <h1>CloudSim Test Results - {timestamp}</h1>
        {content}
    </body>
    </html>
    """

    # Create summary table
    summary_table = """
        <table class="summary-table">
            <tr>
                <th>Test Case</th>
                <th>Status</th>
                <th>SLA0</th>
                <th>SLA1</th>
                <th>SLA2</th>
                <th>SLA3</th>
                <th>Energy (KW-Hour)</th>
                <th>Runtime (s)</th>
            </tr>
    """
    
    # Track overall statistics
    total_tests = len(results)
    passed_tests = 0
    failed_tests = []
    
    for testcase_name, test_data in sorted(results.items()):
        sla_compliances = {0: None, 1: None, 2: None, 3: None}
        testcase_passed = True
        
        # Fill in compliance values
        for sla in test_data['sla_results']:
            sla_compliances[sla['sla_id']] = {
                'compliance': sla['compliance'],
                'required': sla['required'],
                'passed': sla['passed']
            }
            if not sla['passed']:
                testcase_passed = False
        
        if testcase_passed:
            passed_tests += 1
        else:
            failed_tests.append(testcase_name)
        
        # Generate compliance cells
        compliance_cells = []
        for sla_id in range(4):
            if sla_compliances[sla_id]:
                compliance = sla_compliances[sla_id]['compliance']
                required = sla_compliances[sla_id]['required']
                passed = sla_compliances[sla_id]['passed']
                
                if passed:
                    if compliance >= required + 5:
                        color_class = "compliance-high"
                    else:
                        color_class = "compliance-med"
                else:
                    color_class = "compliance-low"
                
                cell = f'<div class="compliance-cell {color_class}">{compliance:.1f}%</div>'
            else:
                cell = '-'
            compliance_cells.append(cell)
        
        # Create row using concatenation instead of f-strings
        
        status_class = 'pass' if testcase_passed else 'fail'
        status_text = '✓ PASS' if testcase_passed else '✗ FAIL'
        
                # Fix the row generation using a list and join
        row_parts = [
            "<tr>",
            f"<td>{testcase_name}</td>",
            f"<td><span class='testcase-status {status_class}'>{status_text}</span></td>"
        ]
        
        # Add compliance cells
        for cell in compliance_cells:
            row_parts.append(f"<td>{cell}</td>")
            
        # Fix energy and runtime formatting
        energy_value = '-'
        if test_data['energy'] is not None:
            energy_value = f"{test_data['energy']:.2f}"
            
        runtime_value = '-'
        if test_data['runtime'] is not None:
            runtime_value = f"{test_data['runtime']:.2f}"
        
        # Add energy and runtime cells
        row_parts.extend([
            f"<td>{energy_value}</td>",
            f"<td>{runtime_value}</td>",
            "</tr>"
        ])
        
        # Join all parts to create the row
        row = ''.join(row_parts)
        summary_table += row
    
    summary_table += "</table>"

    # Generate detailed test cases
    test_case_template = """
        <div class="test-case">
            <div class="test-header">
                <h2>{testcase_name}</h2>
            </div>
            <table class="sla-table">
                <tr>
                    <th>SLA Level</th>
                    <th>Compliance</th>
                    <th>Required</th>
                    <th>Violations</th>
                    <th>Status</th>
                </tr>
                {sla_rows}
            </table>
            <div class="metrics">
                <div class="metric">
                    <strong>Total Energy:</strong> {energy} KW-Hour
                </div>
                <div class="metric">
                    <strong>Runtime:</strong> {runtime} seconds
                </div>
            </div>
        </div>
    """

    content = []
    for testcase_name, test_data in sorted(results.items()):
        sla_rows = []
        for sla in test_data['sla_results']:
            status_class = "pass" if sla['passed'] else "fail"
            status_text = "✓ PASS" if sla['passed'] else "✗ FAIL"
            
            sla_rows.append(f"""
                <tr class="{status_class}">
                    <td>SLA{sla['sla_id']}</td>
                    <td>{sla['compliance']:.2f}%</td>
                    <td>{sla['required']}%</td>
                    <td>{sla['violations']}/{sla['total']}</td>
                    <td>{status_text}</td>
                </tr>
            """)

        content.append(test_case_template.format(
            testcase_name=testcase_name,
            sla_rows=''.join(sla_rows),
            energy=f"{test_data['energy']:.2f}" if test_data['energy'] else "N/A",
            runtime=f"{test_data['runtime']:.2f}" if test_data['runtime'] else "N/A"
        ))

    # Add overall summary
    summary = f"""
        <div class="summary">
            <h2>Summary</h2>
            <p class="{('pass' if passed_tests == total_tests else 'fail')}">
                Test Status: {passed_tests}/{total_tests} testcases passed
            </p>
            <p>Total testcases: {total_tests}</p>
            <p>Passed testcases: {passed_tests}</p>
            <p>Failed testcases: {total_tests - passed_tests}</p>
            {f'<p class="fail">Failed tests: {", ".join(failed_tests)}</p>' if failed_tests else ''}
        </div>
    """

    html_content = html_template.format(
        timestamp=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        content=summary_table + summary + '\n'.join(content)
    )

    with open(filename, 'w') as f:
        f.write(html_content)

def generate_text_report(results, filename):
    """Generate a text-based report with test results in tabular format"""
    
    # Calculate field widths based on content
    testcase_width = max(len("Test Case"), max(len(tc) for tc in results.keys()))
    energy_width = 12
    runtime_width = 10
    sla_width = 8
    
    # Create header template
    header_template = (
        f"{'Test Case':<{testcase_width}} | "
        f"{'Status':<8} | "
        f"{'SLA0':<{sla_width}} | "
        f"{'SLA1':<{sla_width}} | "
        f"{'SLA2':<{sla_width}} | "
        f"{'SLA3':<{sla_width}} | "
        f"{'Energy':<{energy_width}} | "
        f"{'Runtime':<{runtime_width}}"
    )
    
    separator = "-" * (testcase_width + 8 + sla_width * 4 + energy_width + runtime_width + 14)
    
    with open(filename, 'w') as f:
        f.write(f"CloudSim Test Results - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")
        f.write(header_template + "\n")
        f.write(separator + "\n")
        
        total_tests = len(results)
        passed_tests = 0
        
        for testcase_name, test_data in sorted(results.items()):
            sla_compliances = {0: "-", 1: "-", 2: "-", 3: "-"}
            testcase_passed = True
            
            # Process SLA results and track failures
            for sla in test_data['sla_results']:
                sla_id = sla['sla_id']
                compliance = sla['compliance']
                passed = sla['passed']
                
                sla_compliances[sla_id] = f"{compliance:>6.1f}%" + ("✓" if passed else "✗")
                if not passed:
                    testcase_passed = False  # Mark test as failed if any SLA fails
            
            if testcase_passed:
                passed_tests += 1
            
            # Format energy and runtime
            energy = f"{test_data['energy']:.2f}" if test_data['energy'] else "-"
            runtime = f"{test_data['runtime']:.2f}" if test_data['runtime'] else "-"
            
            # Create row with FAIL status if any SLA failed
            row = (
                f"{testcase_name:<{testcase_width}} | "
                f"{'PASS' if testcase_passed else 'FAIL':<8} | "
                f"{sla_compliances[0]:<{sla_width}} | "
                f"{sla_compliances[1]:<{sla_width}} | "
                f"{sla_compliances[2]:<{sla_width}} | "
                f"{sla_compliances[3]:<{sla_width}} | "
                f"{energy:>{energy_width}} | "
                f"{runtime:>{runtime_width}}"
            )
            
            f.write(row + "\n")
        
        f.write(separator + "\n\n")
        f.write(f"Summary:\n")
        f.write(f"Total testcases: {total_tests}\n")
        f.write(f"Passed testcases: {passed_tests}\n")
        f.write(f"Failed testcases: {total_tests - passed_tests}\n")

def run_testcase(directory, testcase):
    """Run a single testcase and return its output
    
    Args:
        directory: Directory containing the testcase
        testcase: Name of the testcase file
    """
    testcase_path = os.path.join(directory, testcase)
    try:
        result = subprocess.run(["./simulator", testcase_path], 
                              stdout=subprocess.PIPE, 
                              stderr=subprocess.PIPE,
                              text=True)
        
        # Parse results
        sla_results = []
        energy = None
        runtime = None
        
        for line in result.stdout.split('\n'):
            if "SLA" in line and "compliance" in line:
                sla_data = parse_sla_line(line)
                if sla_data:
                    sla_results.append(sla_data)
            elif "Total Energy" in line:
                energy = extract_energy_and_runtime(line)
            elif "Simulation run finished" in line:
                runtime = extract_energy_and_runtime(line)

        return {
            'sla_results': sorted(sla_results, key=lambda x: x['sla_id']),
            'energy': energy,
            'runtime': runtime
        }
        
    except subprocess.CalledProcessError as e:
        print(f"Error running testcase {testcase}: {str(e)}")
        return {
            'sla_results': [],
            'energy': None,
            'runtime': None,
            'error': str(e)
        }
    
def run_parallel_testcases(test_cases):
    """Run testcases in parallel and return results
    
    Args:
        test_cases: List of (directory, testcase) tuples
    """
    results = {}
    max_workers = multiprocessing.cpu_count()
    
    print(f"Running testcases in parallel using {max_workers} workers...")
    
    with ProcessPoolExecutor(max_workers=max_workers) as executor:
        # Submit all testcases
        future_to_testcase = {
            executor.submit(run_testcase, directory, testcase): testcase 
            for directory, testcase in test_cases
        }
        
        # Process results as they complete
        for future in as_completed(future_to_testcase):
            testcase = future_to_testcase[future]
            try:
                result = future.result()
                results[testcase] = result
                print(f"✓ Completed {testcase}")
            except Exception as e:
                print(f"✗ Failed {testcase}: {str(e)}")
                results[testcase] = {
                    'sla_results': [],
                    'energy': None,
                    'runtime': None,
                    'error': str(e)
                }
    
    return results

def extract_energy_and_runtime(line):
    """Extract energy or runtime from a line"""
    energy_match = re.search(r"Total Energy ([\d.]+)KW-Hour", line)
    if energy_match:
        return float(energy_match.group(1))
    
    runtime_match = re.search(r"finished in ([\d.]+) seconds", line)
    if runtime_match:
        return float(runtime_match.group(1))
    
    return None

def get_md_files(directory):
    """Get list of .md files from directory"""
    if not os.path.exists(directory):
        print(f"Warning: Directory {directory} does not exist")
        return []
        
    md_files = []
    for file in os.listdir(directory):
        if file.endswith('.md'):
            md_files.append(file)
            
    if not md_files:
        print(f"Warning: No .md files found in {directory}")
        
    return md_files

def get_test_suite_choice():
    """Get user's choice of test suite to run"""
    while True:
        print("\nSelect test suite to run:")
        print("1. Standard testcases")
        print("2. New testcases")
        print("3. Combined test suite")
        try:
            choice = int(input("Enter choice (1-3): "))
            if 1 <= choice <= 3:
                return choice
            print("Invalid choice. Please enter 1, 2, or 3.")
        except ValueError:
            print("Invalid input. Please enter a number.")

def get_testcases_for_suite(choice):
    """Get list of testcases based on user's choice"""
    base_dir = os.path.dirname(os.path.abspath(__file__))
    std_testcases_dir = os.path.join(base_dir, "testcases")
    new_testcases_dir = os.path.join(base_dir, "new_testcases")
    
    testcases = []
    
    if choice == 1:
        testcases = get_md_files(std_testcases_dir)
        return std_testcases_dir, testcases
    elif choice == 2:
        testcases = get_md_files(new_testcases_dir)
        return new_testcases_dir, testcases
    else:
        # Combined suite
        std_tests = [(std_testcases_dir, tc) for tc in get_md_files(std_testcases_dir)]
        new_tests = [(new_testcases_dir, tc) for tc in get_md_files(new_testcases_dir)]
        return "combined", std_tests + new_tests

def main():
    choice = get_test_suite_choice()
    test_source = get_testcases_for_suite(choice)
    
    if test_source[0] == "combined":
        # Convert combined tests to format for parallel execution
        test_cases = test_source[1]  # Already in (directory, testcase) format
        print("\nRunning combined test suite in parallel...")
        results = run_parallel_testcases(test_cases)
    else:
        testdir, testcases = test_source
        # Convert to same format as combined for consistency
        test_cases = [(testdir, tc) for tc in testcases]
        print(f"\nRunning tests from: {testdir}")
        results = run_parallel_testcases(test_cases)
    
    # Generate reports
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    suite_type = {1: "standard", 2: "new", 3: "combined"}[choice]
    
    text_file = os.path.join("test_results", f"test_report_{suite_type}_{timestamp}.txt")
    html_file = os.path.join("test_results", f"test_report_{suite_type}_{timestamp}.html")
    
    generate_text_report(results, text_file)
    generate_html_report(results, html_file)
    
    print(f"\nReports generated:")
    print(f"Text: {text_file}")
    print(f"HTML: {html_file}")

if __name__ == "__main__":
    main()