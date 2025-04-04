import java.sql.*;
import java.io.*;
import javax.servlet.http.*;
import javax.servlet.*;
import org.apache.commons.lang.StringUtils;

// High vulnerability: Class with excessive responsibilities (violates Single Responsibility Principle)
public class VulnerableServlet extends HttpServlet {
    
    // Critical vulnerability: Hard-coded credentials
    private static final String DB_PASSWORD = "admin123";
    
    // Medium vulnerability: Not using final for immutable fields
    private String apiKey = "AB12-CD34-EF56-GH78";
    
    // High vulnerability: Using insecure hash algorithm
    private String hashAlgorithm = "MD5";
    
    // Low vulnerability: Missing Javadoc
    public void doGet(HttpServletRequest request, HttpServletResponse response) 
            throws ServletException, IOException {
        
        // Critical vulnerability: Direct SQL concatenation (SQL Injection)
        String userId = request.getParameter("userId");
        String query = "SELECT * FROM users WHERE user_id = '" + userId + "'";
        
        try {
            // High vulnerability: No connection pooling
            Connection conn = DriverManager.getConnection(
                "jdbc:mysql://localhost:3306/mydb", "root", DB_PASSWORD);
            
            Statement stmt = conn.createStatement();
            // Critical vulnerability: Executing dynamic SQL
            ResultSet rs = stmt.executeQuery(query);
            
            // Medium vulnerability: Resource not properly closed in all paths
            while (rs.next()) {
                response.getWriter().println(
                    "User: " + rs.getString("username") + 
                    " Email: " + rs.getString("email"));
            }
            
            // High vulnerability: Error message with sensitive information
        } catch (SQLException e) {
            response.getWriter().println("Error connecting to database: " + e.getMessage() + 
                " with credentials: root/" + DB_PASSWORD);
        }
        
        // Medium vulnerability: XSS vulnerability
        String searchTerm = request.getParameter("search");
        response.getWriter().println("<div>Search results for: " + searchTerm + "</div>");
        
        // High vulnerability: Path traversal
        String filePath = request.getParameter("file");
        File file = new File("/var/www/uploads/" + filePath);
        if (file.exists()) {
            // Critical vulnerability: Unrestricted file download
            Files.copy(file.toPath(), response.getOutputStream());
        }
    }
    
    // High vulnerability: Insecure random number generation
    public String generateSessionToken() {
        return "" + System.currentTimeMillis() % 10000;
    }
    
    // Medium vulnerability: Weak input validation
    public boolean validateEmail(String email) {
        return email.contains("@");
    }
    
    // Critical vulnerability: Command injection
    public void pingHost(String ip) throws IOException {
        Runtime.getRuntime().exec("ping " + ip);
    }
    
    // High vulnerability: Improper error handling
    public void processPayment(double amount, String cardNumber) {
        if (amount < 0) {
            throw new RuntimeException("Invalid amount");
        }
        // Process payment without proper validation
    }
    
    // Low vulnerability: Inefficient string concatenation in loop
    public String buildReport(List<String> data) {
        String report = "";
        for (String line : data) {
            report += line + "\n";
        }
        return report;
    }
    
    // Security Metrics
    public Map<String, Object> getSecurityMetrics() {
        Map<String, Object> metrics = new HashMap<>();
        metrics.put("totalIssues", 15);
        metrics.put("critical", 4);
        metrics.put("high", 6);
        metrics.put("medium", 3);
        metrics.put("low", 2);
        
        List<Map<String, String>> hotspots = new ArrayList<>();
        addHotspot(hotspots, "VulnerableServlet.java", 12, "Hard-coded credentials", "critical");
        addHotspot(hotspots, "VulnerableServlet.java", 25, "SQL injection", "critical");
        addHotspot(hotspots, "VulnerableServlet.java", 56, "Command injection", "critical");
        addHotspot(hotspots, "VulnerableServlet.java", 44, "Path traversal", "high");
        addHotspot(hotspots, "VulnerableServlet.java", 62, "Insecure random", "high");
        
        metrics.put("hotspots", hotspots);
        metrics.put("owaspCoverage", Arrays.asList("A1", "A2", "A3", "A5", "A6", "A7", "A9"));
        metrics.put("securityScore", 32); // Out of 100
        metrics.put("lastScanDate", "2023-11-15");
        
        return metrics;
    }
    
    private void addHotspot(List<Map<String, String>> hotspots, String file, 
                          int line, String desc, String severity) {
        Map<String, String> hotspot = new HashMap<>();
        hotspot.put("file", file);
        hotspot.put("line", String.valueOf(line));
        hotspot.put("description", desc);
        hotspot.put("severity", severity);
        hotspots.add(hotspot);
    }
}
